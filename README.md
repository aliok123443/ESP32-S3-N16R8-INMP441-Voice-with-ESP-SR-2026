# Voice Lights — сайт проекта

Статическая страница для GitHub Pages проекта **Voice Lights**: офлайн-голосовое управление реле на YD-ESP32-23 / ESP32-S3 N16R8 с микрофоном INMP441 и ESP-SR.

Страница сформирована по фактическому исходному коду прошивки из `../github-release` (эта папка не менялась).

## Что реализовано в прошивке

- ESP-IDF **5.3.4** и `espressif/esp-sr` **2.4.6**.
- INMP441: SCK = GPIO4, WS = GPIO5, SD = GPIO6, L/R = GND (левый канал).
- Реле: GPIO21, active-high по умолчанию.
- Непрерывное английское распознавание ESP-SR MultiNet5 без wake word.
- Одна команда: `lights`. Для переключения реле она должна быть распознана дважды в течение 2 секунд.
- Wi-Fi AP+STA, локальное имя `voice-lights.local`, встроенная панель устройства и резервная точка настройки на `192.168.4.1`.

## Публикация на GitHub Pages

1. Создайте на GitHub репозиторий или откройте репозиторий, где лежит этот файл.
2. Поместите `index.html` в корень ветки, которую будете публиковать.
3. Откройте **Settings → Pages**.
4. В **Build and deployment** выберите **Deploy from a branch**, затем ветку `main` и папку `/(root)`, после чего сохраните.
5. Через несколько минут сайт будет доступен по адресу, который покажет GitHub Pages.

## Gemini: безопасное подключение

`index.html` сознательно не содержит Gemini API key. Любой ключ и пароль, записанные в JavaScript GitHub Pages, видны каждому посетителю через DevTools или исходный код.

Для работы чата нужен serverless proxy (например, Cloudflare Worker):

1. В секретах Worker сохраните ключ как `GEMINI_API_KEY` — именно это имя читает текущий код Worker `green-dream-ddee`. Существующий secret `GEMINI_API` нужно переименовать или удалить и создать заново с именем `GEMINI_API_KEY`.
2. В Worker выполните запрос к Gemini `generateContent`, добавив системный промт из `index.html` на серверной стороне.
3. Разрешите CORS только для домена вашего GitHub Pages.
4. Допускается вернуть исходный JSON Gemini: текущий `index.html` сам извлекает текст из `candidates[0].content.parts`.
5. В `index.html` замените `CHAT_PROXY_URL` на URL Worker.

Не размещайте ключ в Git, `index.html`, GitHub Secrets, если он затем подставляется в статический HTML, или в публичных issue. Google также рекомендует не публиковать API-ключи в клиентском коде.

### Минимальный Cloudflare Worker

Для Worker `green-dream-ddee` используйте URL `https://green-dream-ddee.alioktun.workers.dev`. В **Settings → Variables and Secrets** создайте secret `GEMINI_API_KEY` со значением ключа Google AI Studio. Секрет не попадёт в Git или браузер.

В текущем описании Worker указана модель `gemini-2.0-flash`. Она больше не подходит для нового production-деплоя; в Quick Editor замените её на поддерживаемую модель, например `gemini-3.5-flash-lite`, как в примере ниже.

```js
const ALLOWED_ORIGIN = 'https://YOUR-USERNAME.github.io';
const SYSTEM_PROMPT = `Ты — технический ассистент открытого проекта Voice Lights.
Контекст: YD-ESP32-23 / ESP32-S3 N16R8; INMP441: VDD=3.3V, GND=GND,
SCK=GPIO4, WS=GPIO5, SD=GPIO6, L/R=GND; реле GPIO21, active-high.
ESP-IDF 5.3.4, ESP-SR 2.4.6, английский MultiNet5. WakeNet отключён.
Единственная команда — LIGHTS (фонемы LiTS); для переключения реле она
распознаётся дважды за 2 секунды. Wi-Fi AP+STA, mDNS voice-lights.local.
Отвечай по-русски, кратко, технически точно и только по этому проекту,
ESP-IDF, ESP-SR, электронике и безопасной отладке. Не выдумывай команды.`;

const cors = {
  'Access-Control-Allow-Origin': ALLOWED_ORIGIN,
  'Access-Control-Allow-Methods': 'POST, OPTIONS',
  'Access-Control-Allow-Headers': 'Content-Type',
  'Vary': 'Origin',
};
const reply = (body, status = 200) =>
  new Response(JSON.stringify(body), {
    status, headers: { ...cors, 'Content-Type': 'application/json; charset=utf-8' },
  });

export default {
  async fetch(request, env) {
    if (request.method === 'OPTIONS') return new Response(null, { headers: cors });
    if (request.method !== 'POST' || new URL(request.url).pathname !== '/api/chat') {
      return reply({ error: 'Not found' }, 404);
    }
    if (request.headers.get('Origin') !== ALLOWED_ORIGIN) {
      return reply({ error: 'Forbidden origin' }, 403);
    }

    let payload;
    try { payload = await request.json(); } catch { return reply({ error: 'Bad JSON' }, 400); }
    const contents = Array.isArray(payload.contents) ? payload.contents.slice(-10) : [];
    const validContents = contents
      .filter(item => item && (item.role === 'user' || item.role === 'model') && Array.isArray(item.parts))
      .map(item => ({
        role: item.role,
        parts: item.parts
          .filter(part => typeof part?.text === 'string')
          .map(part => ({ text: part.text.slice(0, 1500) })),
      }))
      .filter(item => item.parts.length);
    if (!validContents.length || validContents.at(-1).role !== 'user') {
      return reply({ error: 'Invalid conversation' }, 400);
    }

    const gemini = await fetch(
      'https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent',
      {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'x-goog-api-key': env.GEMINI_API_KEY },
        body: JSON.stringify({
          system_instruction: { parts: [{ text: SYSTEM_PROMPT }] },
          contents: validContents,
          generationConfig: { maxOutputTokens: 500, temperature: 0.2 },
        }),
      },
    );
    const data = await gemini.json();
    if (!gemini.ok) return reply({ error: 'Gemini request failed' }, 502);
    return reply(data);
  },
};
```

После публикации Worker скопируйте его адрес в переменную `CHAT_PROXY_URL` в `index.html`. Для собственного домена укажите в `ALLOWED_ORIGIN` именно этот домен. Перед публичным запуском добавьте rate limiting / Turnstile: иначе любой посетитель сайта сможет расходовать вашу квоту Gemini.
