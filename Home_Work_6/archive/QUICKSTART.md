# Быстрый старт — PlatformIO + STM32 CubeMX

## ⚡ Проверка работы

```bash
# 1. Компиляция
pio run -e debug

# 2. Загрузка
pio run -e debug -t upload
```

Если успешно — увидите:
```
wrote XXXXX bytes from file .pio/build/debug/firmware.elf
========================= [SUCCESS]
```

---

## 🔧 Настройка пути к OpenOCD (ПЕРВЫЙ ЗАПУСК!)

Путь к OpenOCD **зависит от вашего пользователя Windows**.

### Как найти свой путь:

**Вариант 1 (быстро):**
1. Откройте терминал в VS Code (**Ctrl+`**)
2. Введите:
   ```bash
   python -c "import os; print(os.path.expanduser('~/.platformio/packages/tool-openocd/openocd/scripts'))"
   ```
3. Скопируйте вывод

**Вариант 2 (через проводник):**
1. Нажмите **Win+R**
2. Введите: `%USERPROFILE%\.platformio\packages\`
3. Найдите папку `tool-openocd`
4. Скопируйте путь к `openocd\scripts`

### Измените `platformio.ini`:

Откройте файл и замените путь:

```ini
upload_command = openocd -s "C:/Users/ВАШЕ_ИМЯ/.platformio/packages/tool-openocd/openocd/scripts" ...
```

**Пример:**
```ini
upload_command = openocd -s "C:/Users/Ivanov/.platformio/packages/tool-openocd/openocd/scripts" ...
```

---

## 🔄 После изменений в CubeMX

1. Сохранить и сгенерировать код в CubeMX
2. Очистить сборку:
   ```bash
   pio run -e debug -t clean
   ```
3. Скомпилировать заново:
   ```bash
   pio run -e debug
   ```

---

## 📁 Что можно удалить

### Симлинк `src/` (НЕ используется)
```bash
rmdir src
```

### Папки сборки
```bash
# PlatformIO
rmdir /s /q .pio

# CubeIDE
rmdir /s /q Debug
```

---

## 🔧 Если что-то пошло не так

### 1. Ошибка компиляции HAL
```bash
python fix_hal_const.py
pio run -e debug -t clean
pio run -e debug
```

### 2. Ошибка загрузки
Проверить подключение ST-Link и путь в `platformio.ini`:
```ini
upload_command = openocd -s "ПУТЬ/К/OpenOCD/scripts" ...
```

### 3. Полная пересборка
```bash
# Очистить всё
pio run -e debug -t clean
pio run -e release -t clean

# Скомпилировать заново
pio run -e debug
```

---

## 📋 Команды PlatformIO

| Команда | Описание |
|---------|----------|
| `pio run` | Компилировать все среды |
| `pio run -e debug` | Компилировать debug |
| `pio run -e release` | Компилировать release |
| `pio run -t upload` | Загрузить в MCU |
| `pio run -t clean` | Очистить сборку |
| `pio device list` | Показать подключённые устройства |

---

**Совет:** Используйте VS Code с расширением PlatformIO для удобной разработки!
