# ESP32-S3-N16R8-INMP441-Voice-with-ESP-SR
Offline ESP32-S3 voice-controlled relay with ESP-SR / Офлайн голосовое управление реле на ESP32-S3
# Voice Lights / Голосовое управление светом

[Русский](#русский) · [English](#english)

Offline voice-controlled relay firmware for **YD-ESP32-23 (ESP32-S3 N16R8)**. It uses an INMP441 I2S microphone, ESP-SR MultiNet5 command recognition, and a small web dashboard. No cloud service is required after flashing.

> Status: this version continuously listens for the `lights` command and toggles a relay on GPIO21. Wake-word recognition and direct WS2812 control are not enabled in the current firmware.

## Русский

### Возможности

- Офлайн-распознавание английской команды `lights` через ESP-SR MultiNet5.
- Для срабатывания команда должна быть распознана дважды в течение 2 секунд — защита от случайного включения.
- Реле по умолчанию подключается к GPIO21; уровень по умолчанию active-high.
- Веб-панель: текущий IP устройства или `http://voice-lights.local`.
- Точка доступа настройки доступна всегда: `http://192.168.4.1`.

### Оборудование и подключение

| Устройство | ESP32-S3 |
| --- | --- |
| INMP441 VDD | 3.3 V |
| INMP441 GND | GND |
| INMP441 SCK | GPIO4 |
| INMP441 WS | GPIO5 |
| INMP441 SD | GPIO6 |
| INMP441 L/R | GND (левый канал) |
| Вход реле | GPIO21 |

Не подключайте силовую часть сети к ESP32 напрямую. Используйте исправный изолированный модуль реле и соблюдайте требования электробезопасности.

### Сборка и прошивка

1. Установите ESP-IDF **v5.3.4** и активируйте окружение согласно [официальной инструкции Espressif](https://docs.espressif.com/projects/esp-idf/en/v5.3.4/esp32s3/get-started/).
2. Склонируйте репозиторий и настройте личные параметры:

   ```powershell
   git clone https://github.com/YOUR-USERNAME/voice-lights.git
   cd voice-lights
   # Отредактируйте main/app_config.h: Wi-Fi и пароль точки доступа.
   idf.py set-target esp32s3
   idf.py build flash monitor
   ```

Первый запуск `idf.py build` скачает ESP-SR и другие компоненты через ESP Component Manager, а также соберёт модели в раздел `model`.

### Настройка

До сборки замените в `main/app_config.h` следующие шаблонные значения:

- `APP_WIFI_SSID` и `APP_WIFI_PASSWORD` — данные вашей домашней сети;
- `APP_SETUP_AP_PASSWORD` — уникальный пароль точки доступа;
- `APP_MDNS_HOSTNAME` — желаемое локальное имя.

Не публикуйте этот файл с реальными паролями. Перед коммитом верните плейсхолдеры или храните личную конфигурацию вне репозитория.

## English

### Features

- Offline recognition of the English `lights` command using ESP-SR MultiNet5.
- A command must be recognized twice within two seconds, reducing accidental switching.
- Relay output defaults to GPIO21 and active-high logic.
- Web dashboard at the device IP address or `http://voice-lights.local`.
- A fallback setup access point is always available at `http://192.168.4.1`.

### Hardware wiring

| Device | ESP32-S3 |
| --- | --- |
| INMP441 VDD | 3.3 V |
| INMP441 GND | GND |
| INMP441 SCK | GPIO4 |
| INMP441 WS | GPIO5 |
| INMP441 SD | GPIO6 |
| INMP441 L/R | GND (left channel) |
| Relay input | GPIO21 |

Never connect mains power directly to an ESP32. Use a suitably rated isolated relay module and follow local electrical-safety rules.

### Build and flash

1. Install and export **ESP-IDF v5.3.4** using Espressif’s [official getting-started guide](https://docs.espressif.com/projects/esp-idf/en/v5.3.4/esp32s3/get-started/).
2. Clone the repository, configure it, then build:

   ```powershell
   git clone https://github.com/YOUR-USERNAME/voice-lights.git
   cd voice-lights
   # Edit main/app_config.h with your Wi-Fi settings and AP password.
   idf.py set-target esp32s3
   idf.py build flash monitor
   ```

The first build downloads ESP-SR and the remaining dependencies through ESP Component Manager, then packages the selected models into the `model` partition.

### Configuration and security

Replace the placeholders in `main/app_config.h` before building. Do not publish real Wi-Fi credentials or a real access-point password. Revert the file to its placeholders before committing, or keep your personal configuration outside the repository.

## Project layout

| Path | Purpose |
| --- | --- |
| `main/main.c` | Firmware source |
| `main/app_config.h` | Board pins and local network configuration |
| `main/web/index.html` | Embedded web dashboard |
| `sdkconfig.defaults` | ESP-IDF and ESP-SR defaults |
| `partitions.csv` | 16 MB flash partition table, including ESP-SR models |

## Contributing

Issues and pull requests are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md), keep pull requests focused, and do not include build directories, generated files, or credentials.

## License and third-party software

This repository’s original source is licensed under [MIT](LICENSE). ESP-IDF, ESP-SR, and Component Manager dependencies remain subject to their own licenses; they are downloaded separately during the build.
