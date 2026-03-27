# Pico2Vibe Web Bench

Plataforma web de teste offline baseada diretamente na engine atual de [univibe_rp2350_dma.cpp](C:/progs/pico/pico2vibe/univibe_rp2350_dma.cpp).

## Objetivo

- Simular o comportamento do efeito atual do RP2350 no navegador.
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
