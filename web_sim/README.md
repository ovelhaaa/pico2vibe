# Pico2Vibe Web Bench

Plataforma web de teste offline alinhada ao core compartilhado atual em [`src/dsp/vibe_core.hpp`](../src/dsp/vibe_core.hpp).

## Objetivo

- Simular o comportamento do efeito atual do RP2350 no navegador, incluindo os parâmetros de pós-processamento adicionados ao core compartilhado (`pre_hpf_hz`, `tone_tilt`, `sat_asymmetry` e `sat_out_trim`).
- Processar audio offline em blocos de `32` samples a `44.1 kHz`.
- Espelhar a interface fisica atual: encoder, short press, long press e LEDs.

## Como abrir

O navegador precisa servir os arquivos por HTTP por causa de modulos e audio. Exemplo:

```powershell
cd C:\progs\pico\pico2vibe\web_sim
python -m http.server 8000
```

Depois abra `http://localhost:8000`.

## Fluxo

1. Carregar um WAV ou MP3
2. Converter para stereo `44.1 kHz`
3. Renderizar a saida com a engine simulada
4. Ouvir entrada e saida
5. Exportar o render em WAV

## Observacoes

- A simulacao e offline; nao usa `AudioWorklet` em tempo real.
- A pasta `desktop_tools` nao e usada como base desta bancada.
- Para uma prévia bit-a-bit mais próxima do firmware, use o preview WASM em `web/`, que compila o mesmo C++ do firmware.
