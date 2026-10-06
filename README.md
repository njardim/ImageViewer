# imageViewer

Visualizador de imagens multiplataforma (Windows, macOS, Linux) em C++20 e Qt 6.11. Tem uma interface minimalista e uma **fidelidade de cor verificável em SDR e HDR**. A cobertura de formatos visada é a união de qView, ImageGlass e FFmpeg.

- Plano, decisões e estado atual: [`docs/PLANO.md`](docs/PLANO.md)
- Regras para quem trabalha no código (incluindo o Claude): [`CLAUDE.md`](CLAUDE.md)

## Compilar

As dependências de produção vêm do vcpkg (manifesto `vcpkg.json`) e o Qt 6.11 vem dos binários oficiais.

```
cmake --preset <windows|macos|linux>      # requer VCPKG_ROOT e -DCMAKE_PREFIX_PATH=<Qt>
cmake --build --preset <windows|macos|linux>
tests/smoke.sh <exe>                       # teste sem interface do pipeline de cor
```

O executável `<exe>` é `build/linux/imageViewer` em Linux, `build/windows/imageViewer.exe` em Windows e `build/macos/imageViewer.app/Contents/MacOS/imageViewer` em macOS.

`tests/render_test.py <exe>` verifica o estágio de saída (SDR/EDR/scRGB/PQ e tone mapping, lido da GPU): em Windows usa D3D11 e em macOS usa Metal. Em Linux corre com Xvfb e escolhe `vulkan` ou `opengl` no 2.º argumento. Também em Linux, com Xvfb, `tests/screen_test.py <exe> [vulkan|opengl]` compara os píxeis no ecrã a 100 %.

Para desenvolver em Linux sem vcpkg, use `scripts/build-qt-linux.sh` (compila o Qt a partir do código-fonte) e depois o preset `linux-system`.

## Utilização

`imageViewer [ficheiro|pasta]`. Use `imageViewer --info <ficheiro>` para ver o que o pipeline de cor deteta, e `imageViewer --render <ficheiro> --output sdr|edr|scrgb|pq` para desenhar a imagem fora do ecrã e comparar a GPU com a referência em CPU (`--help` lista as opções).

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
| T | Tone mapping BT.2390 ligado / desligado (desligado: corte no pico) |
| C | Aviso de píxeis alterados (cortados ou com tone mapping) |
| Ctrl/⌘+O | Abrir |
| Botão direito | Menu |

Para forçar o modo de saída, use `IMAGEVIEWER_OUTPUT=sdr|hdr10`.

## Pacotes e releases

O CI gera três pacotes: `.zip` para Windows x64, `.dmg` para macOS arm64 (macOS 13 ou posterior) e `.tar.gz` para Linux x64. Cada pacote é testado numa máquina limpa, sem o Qt nem as bibliotecas do vcpkg. Os pacotes ainda não estão assinados.

O pacote Linux é compilado em Ubuntu 24.04. Por isso, precisa da glibc e da libstdc++ de uma distribuição de 2024 ou mais recente, e de X11 ou XWayland. Precisa também das bibliotecas xcb do sistema, como a `libxcb-cursor0`. O executável é `bin/imageViewer`.

Para publicar uma versão, crie no GitHub uma *release* chamada `vX.Y` ou `vX.Y-sufixo` (por exemplo `v0.2` ou `v0.3-beta`), com uma tag nova com o mesmo nome. Em alternativa, envie só a tag (`git push origin vX.Y`). O CI compila e testa os três pacotes e anexa-os à *release*, juntamente com o `SHA256SUMS`. Se a *release* ainda não existir, o CI cria-a; uma tag com sufixo fica marcada como pré-release.

## Licença

[Apache License 2.0](LICENSE). Ver também [`NOTICE`](NOTICE): a marca "Cristallumnis" não está incluída na licença. As contribuições são aceites com *sign-off* DCO (`git commit -s`).
