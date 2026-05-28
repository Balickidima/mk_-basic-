# PlatformIO + STM32 CubeMX — Рабочая конфигурация

## 📋 Описание

Рабочая конфигурация для компиляции, прошивки и **отладки** STM32 проектов из CubeMX в PlatformIO **без использования framework**.

### Ключевые особенности:
- ✅ **Без framework** — используется HAL из проекта (Drivers/STM32F4xx_HAL_Driver/)
- ✅ **Полная совместимость с CubeIDE** — проект компилируется и там, и там
- ✅ **Автоматическое исправление HAL** — fix_hal_const.py запускается перед каждой компиляцией
- ✅ **Поддержка любых MCU STM32** — F0, F1, F3, F4, F7, H7, L0, L1, L4, G0, G4, U5 и т.д.
- ✅ **Загрузка через OpenOCD** — работает с ST-Link v2
- ✅ **🔥 ПОЛНОЦЕННАЯ ОТЛАДКА** — cortex-debug, GDB 9+, ITM/SWO, точки останова

---

## 🗂 Структура проекта

```
testled6/
├── Core/
│   ├── Inc/           # Заголовочные файлы проекта
│   ├── Src/           # Исходники проекта (main.c, stm32f4xx_it.c и т.д.)
│   └── Startup/       # Файл прерываний (startup_stm32f401ccux.s)
├── Drivers/
│   ├── CMSIS/         # CMSIS библиотеки
│   └── STM32F4xx_HAL_Driver/  # HAL драйверы от STM
├── Debug/             # Артефакты сборки CubeIDE (Makefile)
├── archive/           # 📦 Рабочие файлы конфигурации (ЭТА ПАПКА)
├── .pio/              # Артефакты сборки PlatformIO
├── boards/            # Кастомные board-файлы (создаются автоматически)
├── .vscode/           # 🔥 Настройки отладки VS Code (НОВОЕ!)
│
├── platformio.ini     # ⚙️ Конфигурация PlatformIO
├── extra_script.py    # 🔧 Скрипт настройки сборки
├── fix_hal_const.py   # 🔨 Исправление const в HAL
├── fix_asflags.py     # 🔥 Исправление флагов ассемблера (НОВОЕ!)
├── copy_artifacts.py  # 📋 Копирование артефактов в Debug/
└── testled6.ioc       # Файл проекта CubeMX
```

---

## ⚙️ Используемые файлы

### 1. `platformio.ini`

Основной конфигурационный файл PlatformIO.

**Ключевые настройки:**
```ini
framework =              ; ПУСТО — не используем framework PlatformIO
upload_protocol = custom ; Кастомная загрузка через OpenOCD
```

**Важно: Путь к OpenOCD**

Путь к скриптам OpenOCD **зависит от вашей системы**:

### Windows:
```
C:\Users\ВАШЕ_ИМЯ\.platformio\packages\tool-openocd\openocd\scripts
```

**Пример:**
```ini
upload_command = openocd -s "C:/Users/Alex Pronin/.platformio/packages/tool-openocd/openocd/scripts" ...
```

### macOS:
```
/Users/ВАШЕ_ИМЯ/.platformio/packages/tool-openocd/openocd/scripts
```

### Linux:
```
/home/ВАШЕ_ИМЯ/.platformio/packages/tool-openocd/openocd/scripts
```

---

## 🔎 Как найти путь к OpenOCD

### Способ 1: Через терминал (рекомендуется)

1. Откройте **VS Code**
2. Нажмите **Ctrl+`** (открыть терминал)
3. Введите:

```bash
python -c "import os; print(os.path.expanduser('~/.platformio/packages/tool-openocd/openocd/scripts'))"
```

### Способ 2: Через проводник (Windows)

1. Нажмите **Win+R**
2. Введите: `%USERPROFILE%\.platformio\packages\`
3. Нажмите Enter
4. Найдите папку `tool-openocd`
5. Перейдите в `openocd\scripts`
6. Скопируйте путь из адресной строки

### Способ 3: Через Python

Запустите Python (любой) и выполните:

```python
import os
print(os.path.expanduser("~/.platformio/packages/tool-openocd/openocd/scripts"))
```

---

**Примечание:** PlatformIO устанавливается отдельно от VS Code, даже если вы ставили его через расширение. Все пакеты хранятся в папке `.platformio` в профиле пользователя.

---

### 2. `extra_script.py`

Автоматически настраивает сборку:

1. **Парсит Makefile от CubeMX** (из папки Debug/)
   - Извлекает флаги компиляции
   - Извлекает include paths
   - Извлекает список исходников

2. **Запускает fix_hal_const.py** перед компиляцией

3. **Создаёт статическую библиотеку** из всех файлов проекта:
   - Core/Src/*.c
   - Core/Startup/*.s
   - Drivers/STM32F4xx_HAL_Driver/Src/*.c
   - Drivers/CMSIS/...

4. **Настраивает компилятор**:
   - Разделяет флаги для CC и AS (ассемблеру только -mcpu, -mfpu и т.д.)
   - Добавляет C/C++ стандарты (gnu11, gnu++17)

---

### 3. `fix_hal_const.py`

Исправляет баг в HAL от STM:

**Проблема:**
```c
// В .c файле:
HAL_StatusTypeDef HAL_RCC_OscConfig(const RCC_OscInitTypeDef *RCC_OscInitStruct)

// В .h файле (НЕПРАВИЛЬНО):
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *RCC_OscInitStruct)
```

**Решение:** Добавляет `const` в объявление функции в `.h` файле.

**Когда запускать:**
- Автоматически: перед каждой компиляцией в PlatformIO
- Вручную: после регенерации кода в CubeMX

---

### 4. `copy_artifacts.py`

Копирует артефакты сборки (`.elf`, `.bin`, `.map`) из `.pio/build/debug/` в папку `Debug/` для единообразия с CubeIDE.

---

## 🚀 Команды

### Компиляция
```bash
pio run -e debug      # Debug версия
pio run -e release    # Release версия
```

### Загрузка в MCU
```bash
pio run -e debug -t upload
```

### Очистка
```bash
pio run -e debug -t clean
```

---

## 🔧 Требования

### Установленное ПО:
1. **PlatformIO** (расширение VS Code или через pip)
2. **STM32CubeMX** (для генерации кода)
3. **OpenOCD** (идёт с PlatformIO: `tool-openocd`)
4. **Python 3.x** (для скриптов)

### Драйверы:
- **ST-Link v2** драйвер (для Windows)

---

## 📝 Workflow

### 1. Настройка в CubeMX
1. Откройте `testled6.ioc` в STM32CubeMX
2. Настройте пины, такты, периферию
3. Сохраните и сгенерируйте код (**Project → Generate Code**)

### 2. Компиляция в PlatformIO
```bash
pio run -e debug
```

### 3. Загрузка
```bash
pio run -e debug -t upload
```

### 4. Отладка
- Используйте VS Code + PlatformIO
- Или экспортируйте в CubeIDE для отладки

---

## ⚠️ Важные примечания

### 1. Симлинк `src/`
В проекте есть симлинк `src/ → Core/Src`, но он **НЕ используется** в текущей конфигурации.

Сборка работает через `StaticLibrary` в `extra_script.py`, который явно добавляет все файлы.

**Можно удалить симлинк:**
```bash
rmdir src
```

### 2. Путь к OpenOCD
В `platformio.ini` путь жёстко задан:
```ini
upload_command = openocd -s "C:/Users/Alex Pronin/.platformio/packages/..."
```

**При переносе на другой ПК:**
- Замените путь на актуальный
- Или добавьте OpenOCD в PATH и используйте просто `openocd`

### 3. После регенерации в CubeMX
1. CubeMX перезапишет файлы в `Core/` и `Drivers/`
2. `fix_hal_const.py` автоматически исправит `.h` файл при следующей компиляции
3. **Не забудьте** сделать `pio run -t clean` для чистой пересборки

---

## 🐛 Решение проблем

### Ошибка: `conflicting types for 'HAL_RCC_OscConfig'`
**Решение:** Запустить `fix_hal_const.py` вручную:
```bash
python fix_hal_const.py
```

### Ошибка: `Can't find interface/stlink.cfg`
**Решение:** Проверить путь к скриптам OpenOCD в `upload_command`.

### Ошибка: `couldn't open firmware.elf`
**Решение:** Убедиться, что файл существует в `.pio/build/debug/`.

### CubeIDE не компилирует после PlatformIO
**Причина:** `fix_hal_const.py` изменил `.h` файл.
**Решение:** Перегенерировать код в CubeMX (он восстановит оригинальные файлы).

---

## 🔧 Отладка

### Требования для отладки:

1. **Установите расширения VS Code:**
   - PlatformIO IDE
   - Cortex-Debug (обязательно!)
   - C/C++ (рекомендуется)

2. **Проверьте настройки:**
   - `.vscode/launch.json` — конфигурации отладки
   - `.vscode/settings.json` — настройки cortex-debug
   - `platformio.ini` — флаги отладки и toolchain

3. **Скомпилируйте с отладочными флагами:**
   ```bash
   pio run -e debug
   ```

### Запуск отладки:

1. Откройте `main.c`
2. Поставьте точки останова (F9)
3. Нажмите **F5**
4. Выберите **"Debug (OpenOCD)"**

### Возможности отладки:

✅ **Точки останова** — обычные, условные, аппаратные  
✅ **Шаги** — в функцию, через функцию, из функции  
✅ **Переменные** — локальные, глобальные, watch  
✅ **Периферия** — просмотр через SVD файл  
✅ **Memory View** — просмотр памяти  
✅ **ITM/SWO** — вывод в реальном времени  

**Подробное руководство см. в `ОТЛАДКА.md`**

---

## 📦 Что в архиве

В папке `archive/` находятся рабочие версии файлов:

| Файл | Описание |
|------|----------|
| `platformio.ini` | Конфигурация PlatformIO (с отладкой!) |
| `extra_script.py` | Скрипт настройки сборки |
| `fix_hal_const.py` | Исправление HAL |
| `fix_asflags.py` | 🔥 Исправление флагов ассемблера для GCC 14.x |
| `copy_artifacts.py` | Копирование артефактов |
| `.vscode/` | 🔥 Настройки отладки VS Code |
| `README.md` | Эта документация |
| `QUICKSTART.md` | Быстрый старт |
| `ДЛЯ_НОВИЧКОВ.md` | Гайд для новичков |
| `ОТЛАДКА.md` | 🔥 Руководство по отладке |

---

## 📚 Дополнительные ресурсы

- [PlatformIO Documentation](https://docs.platformio.org/)
- [STM32CubeMX User Manual](https://www.st.com/en/development-tools/stm32cubemx.html)
- [OpenOCD Documentation](http://openocd.org/doc/)

---

**Дата создания:** 2026-03-08  
**Версия:** 1.0  
**MCU:** STM32F401CCU6 (BlackPill)  
**IDE:** STM32CubeMX 6.x, PlatformIO, CubeIDE
