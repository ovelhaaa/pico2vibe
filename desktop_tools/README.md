# Desktop Tools (CLI + GUI)

Esta pasta adiciona um fluxo de teste offline no PC sem mexer no firmware do Pico.

## Objetivo

- Reusar automaticamente o mesmo núcleo DSP do arquivo `../univibe_rp2350_dma.cpp`.
- Permitir testar em arquivo completo (WAV/MP3) antes de gravar na placa.

## Como a paridade com o Pico funciona

No build da CLI, o script `scripts/extract_dsp.py` extrai o trecho DSP direto de `../univibe_rp2350_dma.cpp` e gera:

- `build/desktop_tools/generated/vibe_core.generated.hpp`

Sempre que o DSP do Pico mudar, a CLI recompila usando esse trecho atualizado.

## Compilar CLI

```powershell
cmake -S desktop_tools -B build/desktop_tools -G Ninja
cmake --build build/desktop_tools -j
```

Executavel gerado:

- `build/desktop_tools/univibe_cli.exe`

## Usar CLI

```powershell
build/desktop_tools/univibe_cli.exe ^
  --input input.wav ^
  --output output.wav ^
  --mode chorus ^
  --rate 1.2 ^
  --depth 0.6 ^
  --width 0.8 ^
  --feedback 0.45 ^
  --seed 1 ^
  --wav-format pcm16
```

Entrada aceita WAV; para MP3 usa `ffmpeg` se estiver no PATH.
Saida recomendada: `--wav-format pcm16` (mais compatível).

## GUI Python

```powershell
python desktop_tools/gui/univibe_gui.py
```

A GUI chama a CLI e permite ouvir a saída. Se `ffplay` estiver no PATH, ele é usado para reprodução.

## Observações

- Para equivalência com o firmware atual, a CLI exige 44.1 kHz.
- Modo `chorus` e `vibrato` estão suportados.