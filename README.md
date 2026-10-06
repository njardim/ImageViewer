# imageViewer

Visualizador de imagens multiplataforma (Windows, macOS, Linux) em C++20 e Qt 6.11. Tem uma interface minimalista e uma **fidelidade de cor verificável em SDR e HDR**. A cobertura de formatos visada é a união de qView, ImageGlass e FFmpeg.

- Plano, decisões e estado atual: [`docs/PLANO.md`](docs/PLANO.md)
- Regras para quem trabalha no código (incluindo o Claude): [`CLAUDE.md`](CLAUDE.md)

## Compilar

As dependências de produção vêm do vcpkg (manifesto `vcpkg.json`) e o Qt 6.11 vem dos binários oficiais.

```
cmake --preset <windows|macos|linux>      # requer VCPKG_ROOT e -DCMAKE_PREFIX_PATH=<Qt>
cmake --build --preset <windows|macos|linux>
tests/smoke.sh build/<preset>/imageViewer  # teste sem interface do pipeline de cor
```

Para desenvolver em Linux sem vcpkg, use `scripts/build-qt-linux.sh` (compila o Qt a partir do código-fonte) e depois o preset `linux-system`.

## Utilização

`imageViewer [ficheiro|pasta]`. Use `imageViewer --info <ficheiro>` para ver o que o pipeline de cor deteta.

| Tecla | Ação |
|---|---|
| ← → | Imagem anterior / seguinte |
| 0 | Ajustar à janela |
| 1 | 100 % (píxeis reais) |
| + / − / roda | Zoom |
| F, duplo clique | Ecrã inteiro |
| R / Shift+R | Rodar |
| H | Espelhar |
| I | Informação |
| E / Shift+E | Exposição ±½ EV |
| C | Aviso de clipping |
| Ctrl/⌘+O | Abrir |
| Botão direito | Menu |

Para forçar o modo de saída, use `IMAGEVIEWER_OUTPUT=sdr|hdr10`.
