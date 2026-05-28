# 🔬 Live Watch — Переменные в реальном времени

## ⚠️ ВАЖНО: Ограничения Cortex-Debug

**Cortex-Debug НЕ поддерживает настоящий Live Watch** для переменных в реальном времени.

**Что доступно:**
| Метод | Обновление | Работает? |
|-------|------------|-----------|
| **Watch панель** | Только на паузе | ✅ Да |
| **Live Watch** | Реальное время | ❌ Нет (ограничено) |
| **SWO/ITM** | Реальное время | ✅ Да (но это вывод) |
| **Memory View** | Только на паузе | ✅ Да |

**Решение:** Используйте **SWO/ITM** для мониторинга в реальном времени или быстро останавливайте отладку (F6/F5).

---

## ❓ Проблема

По умолчанию Cortex-Debug показывает переменные **только на паузе**. Это неудобно когда нужно:
- Следить за изменением переменных в реальном времени
- Отлаживать ШИМ, АЦП, таймеры
- Видеть значения без остановки программы

---

## ✅ Решение

### Важно: Live Watch в Cortex-Debug ограничен

К сожалению, **Cortex-Debug не поддерживает настоящий Live Watch** как в некоторых других отладчиках. 

**Что работает:**
- ✅ Переменные в панели **WATCH** обновляются **только на паузе**
- ✅ **SWO/ITM** вывод в реальном времени (но это не переменные)
- ❌ **Live Watch** — ограниченно поддерживается

### Обходной путь: Используйте WATCH правильно

1. **Добавьте переменные в WATCH:**
   - Откройте "Run and Debug" (Ctrl+Shift+D)
   - В секции "WATCH" нажмите "+"
   - Введите имена переменных

2. **Используйте Quick Stop:**
   - Нажмите **Pause** (⏸️) на панели отладки
   - Посмотрите значения
   - Нажмите **Continue** (▶️)
   - Повторите

3. **Горячие клавиши:**
   - **F6** — Pause (остановить)
   - **F5** — Continue (продолжить)
   - **F10** — Step Over (шаг)

---

### Способ 2: Настройка `.vscode/settings.json`

Добавлено в ваш проект:

```json
{
    "cortex-debug.continuousDisassembly": true,
    "cortex-debug.requestRate": 500,
    "cortex-debug.allowRemoteCommands": true,
    "cortex-debug.showDevDebugOutput": "raw"
}
```

**Что делают настройки:**
- `continuousDisassembly` — непрерывная дизассемблеризация
- `requestRate` — частота обновления (500 мс)
- `allowRemoteCommands` — разрешение команд GDB без остановки

---

### Способ 3: Настройка `.vscode/launch.json`

Добавлено в конфигурацию "Debug (OpenOCD)":

```json
{
    "pollingInterval": 500,
    "watchableExpressions": [
        "main::counter",
        "main::value",
        "global_var"
    ],
    "preLaunchCommands": [
        "monitor poll 500"
    ]
}
```

**Что делают настройки:**
- `pollingInterval` — интервал опроса (500 мс)
- `watchableExpressions` — переменные для мониторинга
- `preLaunchCommands` — команды перед запуском

---

## 🚀 Как использовать Live Watch

### Шаг 1: Подготовьте переменные

В `main.c`:

```c
/* USER CODE BEGIN PV */
static volatile int counter = 0;      // ✅ Статическая volatile
volatile uint32_t adc_value = 0;      // ✅ Глобальная volatile
static int pwm_duty = 50;             // ✅ Статическая
/* USER CODE END PV */

/* USER CODE BEGIN WHILE */
while (1)
{
    counter++;                        // Изменяем
    adc_value = ADC->DR;              // Читаем АЦП
    pwm_duty = (counter % 100);       // Меняем ШИМ
    
    HAL_Delay(100);
}
```

**Важно:** 
- Используйте `volatile` для переменных, которые меняются в прерываниях
- `static` делает переменную видимой в отладчике

---

### Шаг 2: Запустите отладку

1. Нажмите **F5**
2. Выберите **"Debug (OpenOCD)"**
3. Дождитесь остановки на `main()`

---

### Шаг 3: Добавьте переменные в Live Watch

#### Вариант A: Через панель WATCH

1. Откройте вкладку **"Run and Debug"** (Ctrl+Shift+D)
2. Найдите секцию **"WATCH"**
3. Нажмите **"+"** (Add Expression)
4. Введите имя переменной:
   - `counter`
   - `adc_value`
   - `pwm_duty`

#### Вариант B: Через Live Watch (если доступен)

1. Откройте **"Live Watch"** (отдельная панель)
2. Добавьте переменные как в WATCH
3. Они будут обновляться **без остановки**

#### Вариант C: Правой кнопкой в коде

1. В коде нажмите правой кнопкой на переменную
2. Выберите **"Debug: Add to Watch"**
3. Переменная появится в WATCH

---

### Шаг 4: Проверьте обновление

1. Нажмите **F5** (продолжить выполнение)
2. Смотрите на панель **WATCH**
3. Значения должны обновляться каждые 500 мс

---

## ⚠️ Если Live Watch не работает

### Проблема 1: Переменные не показываются

**Причина:** Локальные переменные или оптимизация.

**Решение:**
```c
// ❌ Не работает:
void func() {
    int x = 5;
}

// ✅ Работает:
static int x = 5;
volatile int x = 5;
```

---

### Проблема 2: Значения не обновляются

**Причина:** Не настроен polling или высокая оптимизация.

**Решение:**
1. Проверьте `.vscode/settings.json`:
   ```json
   "cortex-debug.requestRate": 500
   ```

2. Проверьте `platformio.ini`:
   ```ini
   build_flags = -O0 -g3
   ```

3. Пересоберите проект:
   ```bash
   pio run -t clean
   pio run -e debug
   ```

---

### Проблема 3: "Expression cannot be evaluated"

**Причина:** Переменная вне области видимости.

**Решение:**
- Используйте полное имя: `main::counter`
- Или глобальные переменные: `global_value`

---

### Проблема 4: Обновление очень медленное

**Причина:** Большой интервал polling.

**Решение:**
```json
// В settings.json
"cortex-debug.requestRate": 200  // 200 мс вместо 500
```

**Но не ставьте меньше 100 мс** — будет нагрузка на GDB!

---

## 💡 Альтернатива: ITM/SWO для реального времени

Если Live Watch всё равно не устраивает — используйте **ITM/SWO**:

### В коде:

```c
#include <stdio.h>

void ITM_SendString(const char *str) {
    while (*str) {
        ITM_SendChar(*str++);
    }
}

void ITM_SendValue(const char *name, int value) {
    char buf[32];
    sprintf(buf, "%s: %d\n", name, value);
    ITM_SendString(buf);
}

// В main loop:
while (1) {
    counter++;
    ITM_SendValue("CNT", counter);  // Отправляем в реальном времени
    HAL_Delay(100);
}
```

### В отладке:

1. Откройте окно **"SWO"**
2. Выберите **Port 0**
3. Смотрите вывод в реальном времени!

**Преимущества:**
- ✅ Настоящее реальное время (мкс)
- ✅ Не тормозит процессор
- ✅ Можно писать логи

**Недостатки:**
- ❌ Требует настройку ITM
- ❌ Только вывод, нельзя изменить переменную

---

## 📊 Сравнение методов

| Метод | Обновление | Скорость | Нагрузка |
|-------|------------|----------|----------|
| **Watch (на паузе)** | Только на паузе | Мгновенно | Нет |
| **Live Watch** | 200-1000 мс | Медленно | Средняя |
| **ITM/SWO** | Реальное время | Быстро | Минимальная |

---

## 📚 Дополнительные ресурсы

- [Cortex-Debug Wiki](https://github.com/Marus/cortex-debug/wiki)
- [Live Watch Configuration](https://github.com/Marus/cortex-debug/blob/master/docs/messages.md#live-watch)
- [ITM/SWO Guide](https://interrupt.memfault.com/blog/best-and-worst-gdb)

---

**Удачи в отладке!** 🐛→📊→✅
