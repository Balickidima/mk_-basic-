# Руководство по отладке STM32 в VSCode

## Настроенные режимы отладки

### 1. **Debug (OpenOCD)** — основной режим ✅
Запускает отладку с автоматической прошивкой и полным контролем.

**Как запустить:** `F5` → выбрать "Debug (OpenOCD)"

**Возможности:**
- ✅ Автоматическая сборка и прошивка перед отладкой
- ✅ Просмотр переменных при остановке
- ✅ Регистры процессора (Cortex Registers)
- ✅ Регистры периферии через SVD (Cortex Peripherals)
- ✅ Memory View — просмотр памяти по адресам
- ✅ Watch Window — отслеживание переменных
- ✅ Breakpoints — точки останова
- ✅ Call Stack — стек вызовов
- ✅ **ITM/SWO вывод в реальном времени** (ITM Port 0)

---

### 2. **Attach (OpenOCD)** — подключение без прошивки
Подключается к уже работающему МК.

**Как запустить:** `F5` → выбрать "Attach (OpenOCD)"

**Зачем:** Когда нужно отладить уже прошитое устройство без перезапуска.

---

## ITM/SWO — вывод в реальном времени

### Что это?
ITM (Instrumentation Trace Macrocell) позволяет выводить отладочную информацию **без остановки** процессора.

### Как работает в проекте

В коде добавлены функции:
```c
ITM_Printf("Значение: %d\r\n", value);  // Вывод через SWO
```

### Настройки SWO
- **Частота CPU:** 84 MHz
- **Частота SWO:** 2 MHz
- **Порт:** ITM Port 0

### Где видеть вывод

**Вариант 1: Cortex-Debug Console**
- При запуске отладки автоматически открывается консоль "ITM Port 0"
- Вывод отображается в реальном времени

**Вариант 2: OpenOCD Telnet**
```bash
telnet localhost 4444
> itm ports
> itm port 0 on
```

---

## Практические примеры

### 1. Просмотр переменных в Watch Window

Добавь в Watch:
```
led_pwm_active     — флаг PWM режима
pwm_duty           — текущий duty cycle (0-100)
pwm_direction      — направление (0=рост, 1=спад)
loop_count         — счётчик циклов
```

**Для массивов:**
```
*(uint8_t*)0x20000000@16  — 16 байт по адресу
data@16                   — массив из 16 элементов
```

### 2. Просмотр регистров GPIO

В **Cortex Peripherals** найди:
- `GPIOC` → `ODR` — выходной регистр (состояние пинов)
- `GPIOA` → `IDR` — входной регистр (кнопка)

Можно менять значения мышкой в режиме остановки!

### 3. Memory View

Открой Memory View и введи адреса:
- `0x20000000` — начало SRAM (переменные)
- `0x08000000` — начало Flash (код)
- `&led_pwm_active` — адрес конкретной переменной

### 4. Breakpoint Conditions

Правый клик на брейкпоинте → Edit Breakpoint:
```
pwm_duty == 50     — остановка при условии
loop_count > 1000  — остановка после N итераций
```

### 5. Log Points (без остановки)

Правый клик на строке → Add Logpoint:
```
ITM_Printf("PWM duty: %d\n", pwm_duty)
```

Выполнится код, но **не остановится** — вывод в ITM консоль.

---

## Команды PlatformIO

```bash
# Сборка
pio run -e debug

# Прошивка
pio run -e debug -t upload

# Сборка + прошивка
pio run -e debug -t upload

# Очистка
pio run -e debug -t clean
```

---

## Задачи VSCode (Tasks)

| Задача | Описание |
|--------|----------|
| `PlatformIO: Build (debug)` | Сборка debug-версии |
| `PlatformIO: Upload` | Прошивка на МК |
| `PlatformIO: Build & Upload` | Сборка + прошивка |
| `PlatformIO: Clean` | Очистка сборки |
| `OpenOCD: Erase Chip` | Полная очистка Flash |
| `OpenOCD: Flash` | Прошивка через OpenOCD |

**Запуск:** `Ctrl+Shift+P` → Tasks: Run Task

---

## Горячие клавиши

| Клавиша | Действие |
|---------|----------|
| `F5` | Старт/продолжение отладки |
| `F9` | Toggle breakpoint |
| `F10` | Step over |
| `F11` | Step into |
| `Shift+F11` | Step out |
| `Ctrl+Shift+F5` | Restart debug |
| `Ctrl+F5` | Run without debugging |

---

## Решение проблем

### "No variables available"
- Убедись, что сборка в режиме **debug** (`build_type = debug`)
- Проверь флаги `-O0 -g3 -ggdb`
- Остановись на брейкпоинте перед просмотром

### "ITM вывод не работает"
- Проверь, что `ITM_Init()` вызван в `SystemInit`
- Убедись, что `cortex-debug.swoEnabled: true`
- Частота SWO должна соответствовать настройкам

### "ST-Link не подключается"
- Проверь подключение платы
- Установи драйверы ST-Link
- Попробуй `OpenOCD: Erase Chip`

### "Переменные не обновляются"
- Cortex-Debug обновляет переменные **только при остановке**
- Для real-time используй ITM/SWO вывод

---

## Сравнение с STM32CubeIDE

| Функция | CubeIDE | VSCode + Cortex-Debug |
|---------|---------|----------------------|
| Live Variables | ✅ | ❌ (только ITM/SWO) |
| Просмотр регистров | ✅ | ✅ |
| Memory View | ✅ | ✅ |
| SVD просмотр | ✅ | ✅ |
| printf вывод | ✅ (SWO) | ✅ (ITM) |
| Breakpoints | ✅ | ✅ |
| Conditional BP | ✅ | ✅ |

**Итог:** VSCode даёт 95% возможностей CubeIDE, плюс гибкость настройки.

---

## Рекомендации

1. **Для отладки логики** — используй брейкпоинты + Watch Window
2. **Для графиков/логов** — ITM_Printf + запись в файл
3. **для периферии** — Cortex Peripherals (SVD)
4. **для производительности** — Memory View + Cycle Counter (DWT)
