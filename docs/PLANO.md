# imageViewer: plano de ação e estado do projeto

> **Documento vivo e fonte única de verdade.** Qualquer conversa ou sessão nova começa por aqui (o `CLAUDE.md` aponta para este ficheiro).
>
> No fim de cada sessão de trabalho atualizam-se:
> - o §1 (Estado atual);
> - o §2 (Registo de decisões), se houver decisões novas;
> - as caixas de verificação das fases (§9).
>
> Língua: pt-PT para documentação e comunicação; inglês para código e comentários.

**Marcas de evidência:**

| Marca | Significado |
|---|---|
| `[código]` | Análise de código-fonte (repositório e commit indicados). |
| `[doc]` | Documentação ou README oficial. |
| `[teste]` | Medido num contentor Linux x64 (4 vCPU, 2026-10-06). |
| `[conhecimento]` | Conhecimento prévio, não verificado neste projeto. |
| `[inferência]` | Dedução a partir de evidência indireta. |

---

## 1. Estado atual

| Campo | Valor |
|---|---|
| Data | 2026-10-06 |
| Fase | **0: Fundação** (em curso) |
| Ramo de trabalho | `claude/upbeat-bohr-tvnhkd` |
| Último marco | Esqueleto C++/Qt 6.11 a compilar sem avisos. Testes de fumo do pipeline de cor passam (P3→scRGB sem limites, EXR > 1.0, orientação EXIF). **Teste de ecrã em Xvfb: rampa sRGB de 8 bits a 100 % idêntica ao ficheiro, bit a bit, em Vulkan e OpenGL** (critério F8 em SDR, Linux). CI para os 3 sistemas criado. |
| Próximos passos | **1.** Confirmar o CI verde nos 3 sistemas (o 1.º build do vcpkg é longo) e corrigir o que falhar. **2.** Validar manualmente em Windows e macOS, incluindo ecrãs HDR (artefactos do CI). **3.** Fase 1 (§9). |
| Bloqueios | Nenhum. A decisão D-P01 (licença MIT do repositório) continua com o Nuno. |

**O que existe no código** (`src/`, cerca de 1 500 linhas):
- descodificação OIIO com recurso ao `QImageReader`;
- conversão única para scRGB linear em RGBA16F: LittleCMS sem limites, CICP analítico (PQ, HLG, sRGB, BT.1886, γ), espaços de cor do OIIO (incluindo ACES AP0/AP1) e cromaticidades EXR;
- orientação EXIF;
- janela `QWindow` + QRhi: D3D11 no Windows, Metal no macOS, Vulkan ou OpenGL no Linux;
- escolha automática da swapchain: scRGB/EDR quando o ecrã suporta HDR, HDR10, senão SDR;
- `hdrInfo` → escala do branco SDR e pico do ecrã;
- shader com saídas SDR, scRGB e PQ;
- overlay de diagnóstico ao nível do branco SDR;
- zoom no cursor, 100 % exatos, pan, rotação e espelho da vista, exposição, aviso de clipping;
- navegação na pasta com ordenação natural; arrastar e largar; menu de contexto; `--info`.

**Notas para sessões na cloud** (contentor Linux):
- `download.qt.io` e os *releases* do GitHub estão bloqueados pelo proxy `[teste]`. O Qt 6.11 compila-se a partir do GitHub com `scripts/build-qt-linux.sh` (≈15 min com 4 vCPU).
- As dependências de imagem vêm do apt do Ubuntu 24.04 (OIIO 2.4, lcms 2.14).
- Fluxo: `cmake --preset linux-system && cmake --build --preset linux-system && tests/smoke.sh build/linux-system/imageViewer`.
- A plataforma Qt `offscreen` não expõe a janela, por isso não exercita o renderizador. Para isso é preciso Xvfb com o plugin xcb (o script já o compila).
- Medições de desempenho só são válidas com a CPU livre: uma compilação em paralelo multiplicou por 7 o tempo de descodificação medido.

**Notas de CI** (GitHub Actions, run 1 de 2026-10-06):
- No Windows, o aqtinstall 3.3.0 não encontra os metadados do Qt 6.11.2 ("Failed to locate XML data"). O workflow usa a versão de desenvolvimento do aqtinstall só nesse job.
- Linux e macOS instalam o Qt 6.11.2 com o aqtinstall 3.3.0 sem problemas.
- No Windows, o aqtinstall de desenvolvimento (3.3.1.dev166) encontra o Qt 6.11.2, mas o py7zr falha ao extrair o qtsvg. A extração passa a ser feita com o 7-Zip do runner (`--external 7z`).
- **Run 2:**
  - **macOS arm64 verde:** build, testes de fumo, deploy e artefacto.
  - **Linux:** build e testes de fumo verdes. O teste de ecrã falhou só porque faltava o numpy no Python do `setup-python` (corrigido com `pip`).
- O 1.º build das dependências do vcpkg é longo. A cache binária guarda-se mesmo se o job falhar (`if: always()`).
- O workflow cancela execuções antigas do mesmo ramo. Isto é relevante porque os minutos macOS custam 10× num repositório privado `[conhecimento]`.

## 2. Registo de decisões

| ID | Data | Decisão | Razão |
|---|---|---|---|
| D-01 | 2026-10-06 | Produto proprietário, distribuição direta (sem lojas). | Decisão do Nuno. |
| D-02 | 2026-10-06 | ~~Python + PySide6~~ → **substituída pela D-05** | — |
| D-03 | 2026-10-06 | ~~Apenas SDR na v1~~ → **substituída pela D-06** | — |
| D-04 | 2026-10-06 | Rejeitar o Magick.NET. | Em Python era viável mas dominado; em C++ o equivalente direto seria o ImageMagick, e não o .NET (§4.3). |
| D-05 | 2026-10-06 | **C++20 + Qt 6.11** | Desempenho, controlo total das APIs de cor e HDR de cada sistema, e binários menores. É o caminho do qView. |
| D-06 | 2026-10-06 | **HDR de raiz** (PQ, HLG, EXR e float), com saída HDR real onde o sistema o permita. | Uso seguro por profissionais da indústria. |
| D-07 | 2026-10-06 | **Cobertura de formatos ≥ qView ∪ ImageGlass ∪ FFmpeg (image2)** | Padrão máximo da indústria (Anexo A). |
| D-08 | 2026-10-06 | **Estrutura de código mínima:** um executável e cerca de 10 ficheiros-fonte, num único `CMakeLists.txt`. | Gestão do código simples. |
| D-09 | 2026-10-06 | O visualizador é uma `QWindow` com swapchain QRhi própria. Os diálogos secundários usam Widgets. | O backing store dos Widgets não suporta HDR `[código: qtbase 6.11, src/widgets, src/gui/painting]`. O HDR do Qt Quick só é ativável por variável de ambiente (`QSG_RHI_HDR`) `[código: qtdeclarative 6.11, qsgrhisupport.cpp:1542]`. |
| D-10 | 2026-10-06 | O espaço de trabalho interno é **scRGB linear** (primárias BT.709, valores estendidos, 1.0 = branco SDR) em texturas RGBA16F pré-multiplicadas. | Coincide com a saída nativa do Windows (scRGB) e do macOS (EDR), e representa qualquer gama de cor. |
| D-11 | 2026-10-06 | Não se copia código do qView nem do ImageGlass (ambos GPL-3). Servem apenas de referência de comportamento. | Compatibilidade com D-01. |
| D-12 | 2026-10-06 | Transferências CICP 1/6/14/15 (BT.709/601/2020) são descodificadas com a EOTF BT.1886 (γ2.4, preto 0), não com a inversa da OETF. | O conteúdo destes códigos é *display-referred* (vídeo); é a convenção dos leitores de referência. A alternativa fica registada para os testes da Fase 1. |

---

## 3. Visão e âmbito

O imageViewer é um visualizador de imagens multiplataforma (Windows, macOS, Linux). É minimalista e rápido, e tem uma **fidelidade de imagem demonstrável em SDR e HDR**.

O ImageGlass 10 já é multiplataforma e lê mais de 90 formatos `[doc]`. A diferenciação face a ele vem de três coisas:
1. pipeline de cor e HDR correto e verificável;
2. cobertura de formatos de nível profissional (EXR, DPX, RAW, PQ, HLG);
3. interface sem ruído.

**Essencial na v1:**
- **E1. Abrir:** associações de ficheiros, `argv` e `QFileOpenEvent`, diálogo, arrastar e largar, recentes.
- **E2. Navegação na pasta:** ordenação natural, ciclo, *file watcher*, pré-carregamento com limite de memória.
- **E3. Formatos:** todos os da matriz do Anexo A marcados como v1.
- **E4. Fidelidade SDR e HDR:** critérios F1 a F12 e H1 a H6 (§7).
- **E5. Zoom e pan:** ajustar, preencher, 100 % em píxeis reais; zoom no cursor; trackpad; rotação e espelho da vista.
- **E6. Animação:** GIF, WebP, APNG, AVIF, JXL; play/pausa e avanço por frame.
- **E7. Multi-imagem:** TIFF multipágina, EXR multi-parte/camadas, PSD composto.
- **E8. Painel de informação:** cadeia de cor, metadados HDR (MaxCLL, MaxFALL, *mastering display*), EXIF essencial.
- **E9. Ferramentas profissionais mínimas:** valor do píxel em código e em nits, aviso de clipping (píxeis acima do pico do ecrã) e exposição para conteúdo linear.
- **E10. Ações de ficheiro:** lixo com desfazer, mudar nome, copiar imagem/caminho, mostrar na pasta, abrir com.
- **E11. Ecrã inteiro e apresentação.**
- **E12. Integração:** associações e instância única nos 3 sistemas; definições mínimas; tema automático; pt-PT e en-US.
- **E13. Distribuição:** pacotes para os 3 sistemas gerados em CI, com assinatura opcional e recomendada.

**Opcional depois da v1, por ordem de valor:**
- **O1.** Gain maps (ISO 21496-1 / UltraHDR / Apple), se não entrarem na Fase 5.
- **O2.** Tira de miniaturas.
- **O3.** Comparação A/B (sinergia com o upscaling da Cristallumnis, como plugin fora do núcleo).
- **O4.** Saída SDI para monitor de referência (Blackmagic DeckLink SDK).
- **O5.** Frames de contentores de vídeo (MOV/MXF) via libavformat.
- **O6.** Atalhos remapeáveis.

**Fora do âmbito:** edição e gravação de imagens, impressão, fundos de ecrã, nuvem, SDK de plugins, telemetria.

---

## 4. Referências analisadas (resumo, válido)

### 4.1 qView
Base: jurplel/qView, commit `c5eca1c`, 2026-04-04, v7.0, C++/Qt `[código]`.

**Preservar:**
- Minimalismo por omissão: sem barra de menus, fundo `#212121`, janela ajustada à imagem entre 20 % e 70 % do ecrã.
- Descodificação em segundo plano e pré-carregamento dos vizinhos (cache limitada por memória, com o perfil de destino na chave).
- Escala em dois tempos: transformação rápida durante o zoom, depois reamostragem de qualidade à resolução física.
- Zoom ancorado no cursor; ordenação natural; ciclo na pasta.
- Perfil ICC do monitor obtido por sistema: `GetICMProfileW`, `NSWindow.colorSpace`, `_ICC_PROFILE`.
- Associações não intrusivas (`OpenWithProgids`; `CFBundleDocumentTypes` com papel *Viewer*; `.desktop` com `MimeType`); tratamento de `QFileOpenEvent`.

**Simplificar:**
- 6 modos de ordenação passam a 3.
- 6 diálogos passam a um painel de informação e uma janela de definições.
- Saem a abertura por URL e a verificação de atualizações.

**Melhorar:**
- Converte para ARGB32 de 8 bits **antes** de converter a cor (`qvimagecore.cpp`, `readFile`).
- No Linux só suporta X11.
- Descodifica as animações duas vezes.
- Roda re-renderizando píxeis ("extremely inefficient").
- Rasteriza o SVG uma única vez.
- Os formatos modernos dependem de binários kimageformats descarregados durante o build.

### 4.2 ImageGlass 10
Base: d2phap/ImageGlass, commit `2cf91de`, 2026-09-27 `[código]`. Stack: .NET 10 + Avalonia/Skia + Magick.NET Q16-HDRI. Plataformas: Windows, macOS 14+ só arm64, Linux só X11 `[doc]`. Licença Classic GPLv3 mais edições Pro pagas `[doc]`.

**Preservar:**
- Registo de codecs com prioridades: SVG → Skia → Magick como último recurso, com deteção pelo conteúdo do ficheiro (`CodecRegistry.cs`).
- Alta profundidade de bits no caminho rápido.
- Perfil ICC personalizado.
- Rendição HDR→SDR BT.2408 com branco de referência a 203 nits (`HdrToneMappingOptions.cs`).
- Modos de zoom, fundo em xadrez, vista por canal, recarregar quando o ficheiro muda.
- "Definir como predefinido" via `ms-settings:defaultapps`.
- Ativos de empacotamento: Info.plist, `.desktop` com tipos MIME de RAW, MSI, AppImage, Flatpak.

**Simplificar:**
- Cerca de 150 comandos e pincéis `IG_*` passam a ~35 ações.
- 31 páginas de definições passam a 4 secções.
- Saem edição, temas, plugins e impressão.

**Melhorar:**
- A gestão de cor fica desligada para utilizadores não "profissionais" (`QuickSetupWindow.cs:218`).
- Só obtém o perfil do monitor em Windows (`Win32ColorProfileProvider`; não existe fonte em macOS nem Linux).
- O HDR é só *tone mapping* para SDR, sem saída HDR real.
- O recurso ImageMagick é cerca de 3 vezes mais lento a descodificar que o libvips `[teste]`.

### 4.3 Magick.NET (D-04)
**Verificado:**
- O README declara "over 100 major file formats"; o número "90+" é do ImageGlass `[doc]`.
- Existem variantes Q8, Q16 e Q16-HDRI `[doc]`.
- A versão 14.17.2 carrega a partir de Python via pythonnet e .NET 8: ImageMagick 7.1.2-32 Q16-HDRI, 257 entradas legíveis `[teste]`.
- Custo em Python: 372 ms extra no arranque e leitura 3 a 4 vezes mais lenta que o libvips `[teste]`.
- O README do ramo principal indica que a próxima versão suporta macOS só em arm64 `[doc]`.

**Em C++:** se for preciso o motor ImageMagick, usa-se diretamente, sem .NET. A alternativa equivalente com licença MIT é o GraphicsMagick (§5, D-P05).

---

## 5. Stack e dependências

Versões verificadas nos *ports* do vcpkg a 2026-10-06 `[teste: microsoft/vcpkg master]`.

| Componente | Versão | Licença | Papel |
|---|---|---|---|
| C++20, CMake ≥ 3.24, Ninja | — | — | Build |
| **Qt** (Core, Gui/QRhi, Widgets, Network, ShaderTools) | 6.11.2 | LGPLv3 (ligação dinâmica) | UI, renderização GPU (D3D11/12, Metal, Vulkan, OpenGL), `QLocalServer` |
| **OpenImageIO** | 3.1.14 | Apache-2.0 | Descodificador principal (features: libheif, jpegxl, libraw, openjpeg, webp, gif, opencolorio) |
| ↳ libheif / libde265 / dav1d ou aom | 1.23.5 / 1.1.3 / 1.5.4 | LGPL-3 / LGPL-3 / BSD | HEIC, AVIF |
| ↳ libjxl | 0.12.0 | BSD-3 | JPEG XL |
| ↳ LibRaw | 0.22.2 | LGPL-2.1 ou CDDL | RAW |
| ↳ OpenEXR | 3.5.2 | BSD-3 | EXR |
| **FFmpeg** (só avcodec, avformat, swscale, avutil; **sem** `gpl` nem `nonfree`) | 9.0.2 | LGPL-2.1+ | Formatos de cauda longa do image2, APNG, gifv/mjpeg |
| **Little CMS** | 2.19.1 | MIT | ICC → scRGB linear; LUT 3D do ecrã |
| **OpenColorIO** | 2.6.0 | BSD | Espaços com nome (ACES, log de câmara) para EXR e DPX (Fase 5) |
| **lunasvg** | 3.5.0 | MIT | SVG/SVGZ |
| GraphicsMagick (a decidir, D-P05) | 1.3.45 | MIT | Último recurso para formatos legados (WMF, WPG, VIFF, CUT, FAX…) |

**Licenças excluídas:**
- exiv2 (GPL-2): os metadados vêm do OIIO.
- Funcionalidades `gpl` e `nonfree` do FFmpeg.
- x265, incluindo a feature **por omissão** `hevc` do libheif no vcpkg `[teste: vcpkg ports/libheif]`. A descodificação HEIC usa o libde265 sem essa feature.
- Plugins `fastfloat` e `threaded` do lcms (GPL-3.0) `[teste: vcpkg ports/lcms]`.
- Packs GPL do LibRaw.
- Ghostscript (AGPL).

**Ligação.** Triplets vcpkg **dinâmicos** em todas as plataformas (`x64-windows`, `arm64-osx-dynamic`, `x64-linux-dynamic`) para cumprir a LGPL. O Qt vem em binários oficiais dinâmicos (aqtinstall/install-qt-action) em CI.

---

## 6. Arquitetura

### 6.1 Estrutura do repositório (D-08)

```
CMakeLists.txt        um único alvo: imageviewer
CMakePresets.json     presets por SO (toolchain vcpkg)
vcpkg.json            dependências (manifesto)
CLAUDE.md             arranque para sessões novas -> lê este plano
docs/PLANO.md         este documento
src/
  main.cpp            QApplication, argumentos, instância única, QFileOpenEvent
  viewer.h/.cpp       ViewerWindow (QWindow): input, zoom/pan, navegação, overlays, menus
  renderer.h/.cpp     QRhi: swapchain SDR/HDR, texturas, pipeline, modos de saída
  image.h/.cpp        Image + decodeFile(): OIIO -> FFmpeg -> SVG -> Qt; conversão para scRGB linear
  color.h/.cpp        ICC (lcms2), CICP (PQ/HLG/sRGB/...), matrizes, LUT 3D do ecrã
  folder.h/.cpp       listagem, ordenação natural, cache e pré-carregamento
  platform.h          serviços por SO (perfil ICC do ecrã, estado HDR, mostrar na pasta)
  platform_win.cpp | platform_mac.mm | platform_linux.cpp
  shaders/image.vert, image.frag   compilados com qsb no build
resources/            ícones, Info.plist, .desktop, .rc, .iss
scripts/              build-qt-linux.sh (Qt a partir do código-fonte, para sessões cloud)
tests/                testes de fidelidade e corpus de formatos
.github/workflows/build.yml   CI: Windows, macOS, Linux -> artefactos
```

Os ficheiros só se dividem quando ultrapassarem cerca de 800 linhas.

### 6.2 Pipeline de imagem

1. **Identificação do formato** pelo conteúdo do ficheiro. A ordem de tentativa é OIIO (que identifica a maioria pelo conteúdo) → FFmpeg → lunasvg → `QImageReader`.
2. **Descodificação na profundidade nativa:** uint8, uint16, half ou float, com alfa direto.

   O descodificador devolve também um descritor de cor, com prioridade:
   - **a)** ICC embutido;
   - **b)** CICP (nclx em HEIF/AVIF, `cICP` em PNG, codificação enumerada em JXL);
   - **c)** atributos do formato (`chromaticities` do EXR, `oiio:ColorSpace`, transferência do DPX);
   - **d)** nenhum: assume-se sRGB, e a interface assinala-o.

   Devolve ainda a orientação e os metadados HDR (MDCV, CLLI e, mais tarde, gain map).
3. **Orientação** aplicada exatamente uma vez.
4. **Conversão em CPU** (thread de trabalho) para **scRGB linear em float**:
   - ICC: lcms2 com transformação float sem limites (*unbounded*), intenção relativa com BPC.
   - CICP: fórmulas analíticas, com PQ ST 2084 em nits absolutos e HLG BT.2100 com OOTF dependente do pico.
   - EXR: matriz a partir de `chromaticities`.

   Seguem-se a pré-multiplicação e o empacotamento em RGBA16F.
5. **Upload** para textura RGBA16F com mipmaps. Imagens maiores que o limite da GPU (normalmente 16384) são divididas em blocos.
6. **Shader único:** amostragem → exposição (só conteúdo linear) → tone mapping (EETF BT.2390, apenas se o pico do conteúdo exceder o do ecrã) → modo de saída:

   | Modo | Quando | Codificação |
   |---|---|---|
   | `ScRGB` | macOS (sempre: EDR, superfície marcada pelo Qt) · Windows com Advanced Color ativo (HDR ou ACM) · Linux Vulkan com `EXTENDED_SRGB_LINEAR` | linear, 1.0 = branco SDR (macOS) ou 80 nits (Windows: o branco SDR vem de `sdrWhiteLevel`) |
   | `PQ` | swapchain HDR10 (Vulkan/Linux, ou opção no Windows) | BT.2020 + ST 2084, nits absolutos |
   | `SdrIcc` | Windows sem Advanced Color · Linux X11 ou SDR | LUT 3D (lcms2: scRGB → perfil ICC do ecrã) |

7. **Overlays de UI:** pintados com `QPainter` numa textura RGBA8 SDR e compostos ao nível do branco SDR. A UI nunca encandeia em HDR.

**Factos que sustentam o pipeline `[código: qtbase 6.11]`:**
- O `QRhiSwapChain` oferece `SDR`, `HDR10`, `HDRExtendedSrgbLinear` e `HDRExtendedDisplayP3Linear`.
- `hdrInfo()` devolve:
  - nits min/max no D3D e no Vulkan sobre DXGI;
  - `maxColorComponentValue` / `potentialEDRHeadroom` no Metal;
  - `sdrWhiteLevel`.
- No Metal, a `CAMetalLayer` é marcada com `ExtendedLinearSRGB`, `ITUR_2100_PQ` ou `ExtendedLinearDisplayP3`, e o ColorSync faz o *matching* para o ecrã.
- No D3D11 usam-se os espaços DXGI `G22_P709` (SDR), `G10_P709` (scRGB) e `G2084_P2020` (HDR10).
- Os espaços Vulkan são `EXTENDED_SRGB_LINEAR`, `HDR10_ST2084` e `DISPLAY_P3_LINEAR`.
- Há texturas 3D e RGBA16F/32F.

### 6.3 Camada por sistema (`platform_*`)

| Serviço | Windows | macOS | Linux |
|---|---|---|---|
| Perfil ICC do ecrã (modo SdrIcc) | `GetICMProfileW` por monitor | Não necessário (ColorSync) | `_ICC_PROFILE[_n]` (X11), colord |
| Estado Advanced Color / HDR | DXGI `IDXGIOutput6::GetDesc1` e DisplayConfig (ACM) | `hdrInfo` (headroom EDR) | Capacidades da swapchain Vulkan |
| Abertura por associação | `argv` | `QFileOpenEvent` | `argv` |
| Lixo | `QFile::moveToTrash` (os 3) | — | — |
| Mostrar na pasta | `explorer /select,` | `NSWorkspace activateFileViewerSelecting` | D-Bus FileManager1 `ShowItems` |
| Instância única | `QLocalServer` | Sistema (Apple Events) | `QLocalServer` |

---

## 7. Critérios de fidelidade

### 7.1 SDR (F1–F12)

| ID | Critério | Limiar |
|---|---|---|
| F1 | Descodificação | **Sem perdas:** bit a bit igual a um descodificador de referência. **Com perdas:** igual à mesma biblioteca, ou PSNR ≥ 50 dB face a um descodificador independente. |
| F2 | Profundidade de bits | Sem quantização intermédia abaixo de FP16; erro ≤ 1 LSB à profundidade de saída. |
| F3 | Gestão de cor | Face a referência independente (ArgyllCMS `cctiff`, colour-science): ΔE00 médio ≤ 0,5, p99 ≤ 1,0, máximo ≤ 2,0. Corpus: sRGB, P3, Adobe RGB, ProPhoto, Rec.2020, cinzento, CMYK, ICC v2 e v4, perfis LUT, CICP. |
| F4 | Sem perfil | Assume sRGB; o painel indica "assumido". |
| F5 | Perfil ou modo do ecrã | Mudança de ecrã, de perfil ou de HDR on/off → reconversão em ≤ 500 ms. |
| F6 | Transparência | Erro ≤ 1 LSB face a composição de referência; sem halos. |
| F7 | Orientação | EXIF 1 a 8 corretas em todos os formatos, sem dupla aplicação. |
| F8 | Escala | **100 %:** 1 píxel da imagem = 1 píxel físico, idêntico bit a bit com DPR 1; 1,25; 1,5 e 2. **Redução:** mipmaps em luz linear e trilinear, ΔE00 médio ≤ 1 face a referência Lanczos. **Ampliação ≥ 200 %:** vizinho mais próximo, exato. |
| F9 | Animação | Atrasos ±10 ms; disposição e mistura de frames corretas. |
| F10 | Metadados de cor | Coincidem com o ficheiro. |
| F11 | Gain map | A imagem base aparece exata até a Fase 5 aplicar o gain map. |
| F12 | Sem degradação silenciosa | Qualquer degradação fica visível no painel de informação. |

### 7.2 HDR (H1–H6)

| ID | Critério | Limiar |
|---|---|---|
| H1 | Descodificação PQ e HLG | PQ em nits absolutos (ST 2084), erro relativo < 0,1 %. HLG com OOTF BT.2100 para o pico do ecrã. |
| H2 | Mapeamento de saída | Conteúdo ≤ pico do ecrã é reproduzido sem alteração (nits absolutos em PQ; relativo ao branco de referência em HLG e SDR). Verificado por leitura do alvo offscreen. |
| H3 | Branco SDR | SDR e UI ao `sdrWhiteLevel` do sistema (Windows) ou 1.0 EDR (macOS). |
| H4 | Tone mapping | EETF BT.2390 aplicada só acima do joelho; identidade abaixo (por leitura). Opção "sinal sem tone mapping + aviso de clipping". |
| H5 | Metadados HDR | MaxCLL, MaxFALL e *mastering display* mostrados quando existem. |
| H6 | Ecrã SDR | Conteúdo HDR em ecrã SDR é mapeado por operador documentado (BT.2408/2390) e a interface indica-o. |

### 7.3 Limites declarados

A aplicação garante fidelidade **até ao buffer entregue ao sistema**. O que acontece depois está fora do seu controlo:
- **Gama e pico do ecrã:** tone mapping do próprio ecrã, ABL.
- **Headroom EDR do macOS:** varia com o brilho e a luz ambiente.
- **Windows:** o tone mapping do ecrã em HDR10 e a régua de brilho SDR.
- **Linux:** o HDR é imaturo e depende do compositor e do Mesa.
- **Calibração:** a precisão depende da calibração e do perfil do monitor.
- **Ajustes do sistema:** Night Light e True Tone alteram a imagem.

**Não substitui um monitor de referência** ligado por SDI. Ver O4.

---

## 8. Experiência de utilização

**Princípios:**
- A imagem é a interface; não há barras permanentes.
- Os controlos aparecem com o movimento do rato e desaparecem 1,5 s depois. Respeita-se a opção do sistema para reduzir movimento.
- O teclado chega para tudo.
- Não há diálogos modais no fluxo normal.

**Barra flutuante inferior** (aparece ao mover o rato):
- anterior e seguinte (também há zonas clicáveis nas margens);
- nome do ficheiro e posição na pasta;
- percentagem de zoom (alterna entre ajustar e 100 %);
- rotação da vista;
- informação;
- ecrã inteiro.

**Painel de informação (tecla I):**
- ficheiro, dimensões, codec;
- profundidade de bits e alfa;
- **cadeia de cor:** origem → espaço de trabalho → modo de saída e ecrã (SDR/HDR, pico, branco SDR);
- metadados HDR;
- EXIF.

**Menu de contexto** (é a fonte única de ações; o macOS tem também a barra de menus nativa):
- Abrir, Recentes, Abrir com, Mostrar na pasta
- Copiar imagem, Copiar caminho
- Rodar, Espelhar
- Zoom
- Fundo
- Apresentação
- Mudar nome, Lixo
- Informação, Definições

**Atalhos:**

| Tecla | Ação |
|---|---|
| ← → / PgUp PgDn | Anterior / seguinte |
| Home / End | Primeira / última |
| `+` `−` / roda | Zoom |
| `0` | Ajustar |
| `1` | 100 % |
| F / F11 / duplo clique | Ecrã inteiro |
| Esc | Sair do ecrã inteiro |
| R / Shift+R | Rodar ↻ / ↺ |
| H | Espelhar |
| I | Informação |
| Del | Lixo |
| Ctrl/⌘+Z | Desfazer |
| F2 | Mudar nome |
| Ctrl/⌘+C | Copiar imagem |
| Ctrl/⌘+Shift+C | Copiar caminho |
| Ctrl/⌘+O | Abrir |
| K | Pausar animação |
| `,` `.` | Frame anterior / seguinte |
| S | Apresentação |
| E / Shift+E | Exposição +/− |
| C | Aviso de clipping |
| Ctrl/⌘+, | Definições |

**Definições** (cerca de 10):
- tema e fundo;
- perfil de destino SDR (automático ou ICC personalizado);
- modo HDR (automático, forçar SDR) e tone mapping (EETF ou sinal);
- comportamento da roda;
- ordenação; ciclo;
- memória do pré-carregamento;
- confirmar eliminação;
- botão "Associações…".

---

## 9. Fases

Esforço relativo entre parênteses. Estimativa total até à v1: 16 a 24 semanas para um engenheiro sénior com assistência de IA `[estimativa; confiança baixa]`.

### Fase 0 — Fundação (M)
- [x] `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `CLAUDE.md`, `scripts/build-qt-linux.sh`
- [x] Janela QRhi: escolhe a swapchain (SDR, scRGB ou HDR10) por sistema e regista `hdrInfo` em log *(validada em Linux/Xvfb; falta Windows, macOS e HDR real)*
- [x] Descodificação mínima (OIIO + recurso Qt) → scRGB linear → textura → shader com modos de saída
- [x] Ajustar, 100 %, zoom no cursor, pan, anterior/seguinte, arrastar e largar, `QFileOpenEvent` *(falta validar em execução)*
- [~] CI GitHub Actions: Windows x64, macOS arm64, Linux x64 → build, testes de fumo e artefacto de instalação (zip). *DMG e AppImage ficam para a Fase 4.*
- [ ] Medição do arranque até à 1.ª frame e do tamanho dos pacotes

**Aceitação:**
- CI verde nos 3 sistemas.
- O artefacto abre JPEG, PNG de 16 bits, EXR e AVIF PQ.
- Arranque até à 1.ª frame ≤ 300 ms a quente no hardware de referência.
- Em ecrã HDR (MacBook XDR e Windows HDR), a swapchain HDR é escolhida e PQ de 1000 nits mostra realce acima do branco SDR (verificação manual).

### Fase 1 — Núcleo de cor e HDR (L)
- [ ] lcms2: ICC → scRGB; CICP analítico (sRGB, BT.709/1886, gama 2.2/2.6, PQ, HLG, linear); EXR `chromaticities`
- [ ] Modo SdrIcc com LUT 3D do perfil do ecrã; deteção de Advanced Color/ACM no Windows; branco SDR
- [ ] EETF BT.2390; mapeamento HDR→SDR; exposição; aviso de clipping; leitura do valor do píxel (código e nits)
- [ ] Harness de fidelidade: render offscreen e leitura (lavapipe em CI Linux); corpus sintético

**Aceitação:** F1–F7, F10, F12 e H1–H6 em CI.

### Fase 2 — Cobertura de formatos (L)
- [ ] Backend FFmpeg (image2 e APNG), lunasvg, recurso Qt (ICO, CUR, ICNS)
- [ ] Animação (F9), multipágina e multi-parte, blocos para imagens enormes, opções de RAW
- [ ] Matriz do Anexo A com um ficheiro de teste por formato em CI; decisão D-P05 (GraphicsMagick)

**Aceitação:** 100 % das entradas v1 do Anexo A abrem e passam F1/F7; nenhum crash no corpus.

### Fase 3 — Visualizador e UX (L)
- [ ] Cache e pré-carregamento, *file watcher*, overlays, painel de informação, menu de contexto, barra de menus macOS, definições, temas, i18n, acessibilidade, ações de ficheiro

**Aceitação:**
- F8.
- Imagem seguinte já pré-carregada em ≤ 50 ms (p95).
- JPEG de 24 MP nítido em ≤ 300 ms.
- RAM estável ao fim de 1 000 navegações.
- Teste com 5 a 8 utilizadores em 8 tarefas: ≥ 90 % de sucesso.

### Fase 4 — Integração e distribuição (M)
- [ ] Associações:
  - Windows: ProgID, `OpenWithProgids`, Capabilities, `ms-settings:defaultapps`;
  - macOS: UTIs e `LSHandlerRank` Alternate;
  - Linux: `.desktop`, MIME, AppStream.
- [ ] Instância única; eventos de mudança de ecrã, perfil e HDR (F5)
- [ ] Instaladores: Inno Setup; DMG com codesign e notarização; AppImage e Flatpak. Assinatura condicional a segredos de CI.
- [ ] Testes de ponta a ponta das associações nos 3 sistemas

**Aceitação:**
- Duplo clique, `open`, `start` e `xdg-open` abrem o ficheiro certo.
- Um segundo ficheiro reutiliza a instância.
- Desinstalar limpa as associações.

### Fase 5 — Profissional e endurecimento (M)
- [ ] OCIO (ACES, logs de câmara) para EXR/DPX/Cineon; gain maps (ISO 21496-1, UltraHDR, Apple)
- [ ] Fuzzing dos descodificadores, profiling, beta com 10 a 20 utilizadores, release candidate

**Aceitação:** 0 crashes em ≥ 10 000 ficheiros corrompidos; F e H verdes nos 3 sistemas.

---

## 10. Testes

**Corpus sintético** (gerado pelo projeto):
- rampas de 8 e 16 bits e em float;
- patches em várias gamas;
- gradientes de alfa;
- padrão de zonas;
- 8 orientações;
- rampas PQ e HLG de 0 a 10 000 nits.

**Corpus público** (licenças a confirmar):
- PngSuite;
- exif-orientation-examples;
- conformidade do libjxl;
- testes do libavif e do libheif;
- imagens ICC v4 (color.org);
- raw.pixls.us;
- OpenEXR test images.

**Referências independentes:**
- ArgyllCMS (motor de cor próprio);
- colour-science;
- fórmulas analíticas ST 2084 e BT.2100.

Nunca se usa o mesmo motor do produto como referência.

**HDR em CI:** render offscreen em RGBA16F/32F e leitura dos valores.

**HDR manual:** MacBook Pro XDR, ecrã HDR10 em Windows 11, KDE Plasma 6 HDR (melhor esforço). Colorímetro com ArgyllCMS `spotread`.

---

## 11. Distribuição e assinatura

| SO | Build | Pacote | Assinatura |
|---|---|---|---|
| Windows x64 | MSVC, vcpkg `x64-windows`, Qt via aqt | Inno Setup + zip portátil | Azure Trusted Signing ou certificado OV via `signtool` (opcional). Sem assinatura → aviso SmartScreen. |
| macOS arm64 | Clang, `arm64-osx-dynamic` | `.app` + DMG | `codesign --options runtime` + `notarytool` + `stapler`. Exige Apple Developer Program (≈99 USD/ano) `[conhecimento]`. Sem notarização, o Gatekeeper bloqueia e o utilizador tem de autorizar em Definições do Sistema. |
| Linux x64 | GCC, `x64-linux-dynamic`, compilado na base glibc mais antiga suportada | AppImage (linuxdeploy-plugin-qt); Flatpak (runtime KDE) | Não necessária; GPG opcional |

O deploy do Qt faz-se com `qt_generate_deploy_app_script`. As DLL e dylibs do vcpkg são copiadas pelo deploy.

O primeiro build de dependências demora 30 a 90 minutos por sistema `[estimativa]`; depois fica em cache binária do vcpkg (provider `files` + `actions/cache`).

---

## 12. Riscos

| Risco | Mitigação |
|---|---|
| R1. HDR no Linux imaturo (depende do compositor e do Mesa) | Melhor esforço com recurso SDR+ICC. Testar KDE Plasma 6. |
| R2. Diferenças de HDR entre sistemas (headroom EDR variável; régua SDR do Windows; tone mapping do ecrã) | Ler `hdrInfo` em cada frame ou evento; mostrar o modo no painel; limites declarados (§7.3). |
| R3. Builds do vcpkg partidos ou lentos (FFmpeg, OIIO) em algum sistema | Fixar a `builtin-baseline`; *overlay ports*; cache binária; atualizar trimestralmente. |
| R4. Patentes HEVC (libde265) | Parecer jurídico (D-P03). Alternativa: descodificadores do sistema (WIC HEIF, ImageIO) por trás do mesmo backend. |
| R5. Superfície de ataque dos parsers | Dependências atualizadas, fuzzing, sem funcionalidades de rede; opção de isolar descodificadores de risco num processo. |
| R6. Incumprimento LGPL | Ligação dinâmica; avisos de licença no instalador; lista de exclusões (§5). |
| R7. Imagens maiores que o limite de textura ou que a memória | Blocos e pirâmide; limite de memória configurável. |
| R8. Crescimento do âmbito | Contrato de âmbito (§3). |

---

## 13. Decisões pendentes

Nenhuma bloqueia a Fase 0. A proposta indicada é a adotada por omissão.

| ID | Questão | Proposta |
|---|---|---|
| D-P01 | **O repositório tem licença MIT, mas o produto é proprietário (D-01).** | Substituir por licença proprietária e manter o repositório privado. **Decisão do Nuno.** |
| D-P02 | Plataformas e arquiteturas | Windows x64 · macOS arm64 (Intel só se houver procura) · Linux x64. Windows arm64 depois. |
| D-P03 | HEIC (patentes HEVC) | Incluir libde265 só com parecer jurídico; caso contrário, descodificadores do sistema. |
| D-P04 | Tone mapping por omissão quando o conteúdo excede o ecrã | EETF BT.2390, com modo "sinal" (sem TM, com aviso de clipping). |
| D-P05 | GraphicsMagick como último recurso para formatos legados | Decidir no fim da Fase 2, com a matriz em mãos. |
| D-P06 | Vídeo | Fora; só animações curtas via FFmpeg (gifv, mjpeg). Frames de MOV/MXF ficam em O5. |
| D-P07 | Versões mínimas | Windows 10 22H2 (melhor esforço) e 11; macOS: o mínimo do Qt 6.11 (a confirmar); glibc da base de build Linux. |
| D-P08 | Contas de assinatura | Apple Developer ID e Azure Trusted Signing (ou certificado OV). |
| D-P09 | Instância única por omissão; vizinho mais próximo a partir de 200 %; intenção relativa com BPC; sem atualizações automáticas; sem telemetria | Adotado. |

---

## 14. Por verificar

- Comportamento real da saída HDR em hardware: Windows HDR10/scRGB, macOS XDR, KDE Plasma 6 Wayland.
- Interação com o ACM do Windows 11.
- Arrastar e largar numa `QWindow` pura (sem Widgets) nos 3 sistemas.
- Versão mínima de macOS do Qt 6.11; disponibilidade de runners macOS x64 no GitHub.
- Metadados MDCV/CLLI expostos pelo OIIO 3.1 para HEIF/AVIF. O OIIO expõe `CICP` em PNG, HEIF, JXL e FFmpeg `[código: OIIO v3.1.14.0]`; MDCV/CLLI não foram encontrados.
- Suporte JPEG XS e JPEG-LS no FFmpeg 9 do vcpkg com configuração LGPL.

---

## Anexo A — Matriz de formatos (objetivo D-07)

Origem das listas:
- **qView:** Qt + kimageformats (`dist/linux/*.desktop`) `[código]`.
- **ImageGlass 10:** `IMAGE_FORMATS` em `Const.cs:83`, 86 extensões `[código]`.
- **FFmpeg image2:** `libavformat/img2.c`, 66 entradas `[código: FFmpeg master]`.

| Família | Extensões | Backend | v1 |
|---|---|---|---|
| Web/consumo | jpg jpeg jpe jfif jfi, png, gif, webp, avif avifs, heic heif hif, jxl, bmp dib | OIIO | ✔ |
| Animação | apng, gif, webp, avifs, jxl; gifv, mjpeg | OIIO / FFmpeg | ✔ |
| Ícones | ico, cur, icns, ani | Qt (ani: parser próprio, mais tarde) | ✔ (ani depois) |
| Vetor | svg svgz | lunasvg | ✔ |
| Pro/VFX/cinema | exr, dpx, cin, hdr pic (Radiance), pfm phm, tif tiff (float, CMYK, multipágina), psd psb, dds, sgi rgb rgba bw, tga, rla, fits, iff, jp2 j2k j2c jpc | OIIO | ✔ |
| JPEG profissionais | jls (JPEG-LS), jxs (JPEG XS), ljpg | FFmpeg (a verificar) | ✔ se disponível |
| RAW | 3fr ari arw bay cap cr2 cr3 crw dcr dcs dng drf eip erf fff gpr iiq k25 kdc mdc mef mos mrw nef nrw orf pef ptx pxn raf raw rw2 rwl rwz sr2 srf srw x3f | OIIO/LibRaw | ✔ (r3d: RED SDK, fora; gpr: a verificar) |
| Mapas de bits simples | pbm pgm ppm pnm pam, qoi, wbmp, xbm, xpm | OIIO / FFmpeg | ✔ |
| Legados (FFmpeg) | pcx, ras sun sunras im1 im8 im24 im32 rs, xwd, pct pict, pcd, pix (Alias), img ximg timg (GEM), vbn, xface, cri, mng | FFmpeg | ✔ |
| Legados (outros) | wmf emf wpg viff xv cut fax, xcf, kra ora, flif, obm, exif | GraphicsMagick / próprio (kra/ora: `mergedimage.png` no zip) | Decidir em D-P05; flif obsoleto |

---

## Anexo B — Evidência recolhida (2026-10-06)

**Medições em Python (antes da D-05):**
- Arranque PySide6 congelado: 0,21–0,28 s.
- Pillow trunca PNG de 16 bits para 8 bits.
- Qt `QColorSpace` vs LittleCMS: ΔE00 médio de 0,034 (perfis matriz/TRC).
- Leitura de 24 MP até numpy: pyvips 0,57–0,87 s; Magick.NET via pythonnet 3,4–3,9 s.

**Qt 6.11 (qtbase `v6.11.2`, qtdeclarative 6.11)** `[código]`:
- `QRhiSwapChain::Format {SDR, HDR10, HDRExtendedSrgbLinear, HDRExtendedDisplayP3Linear}`.
- `QRhiSwapChainHdrInfo { LuminanceInNits | ColorComponentValue, sdrWhiteLevel }`.
- Backends HDR em D3D11, D3D12, Vulkan e Metal.
- Widgets sem HDR; Qt Quick com HDR só via `QSG_RHI_HDR`.

**Pipeline C++ (build local, Qt 6.11.2 compilado + OIIO 2.4 do apt)** `[teste]`:
- `tests/screen_test.py` em Xvfb: rampa sRGB de 8 bits mostrada a 100 % sem nenhuma diferença face ao ficheiro (0 níveis, 0 % de píxeis), com Vulkan/lavapipe e OpenGL/llvmpipe.
- Descodificação fundida (nativo → half): JPEG 2,5 MP em 54 ms; JPEG 24 MP em 0,9 s e PNG 16 bits 24 MP em 1,4 s, dominados pela leitura do OIIO 2.4. Pico de memória ≈ 270 MB para 24 MP.
- Display P3 vermelho (ICC) → scRGB (1,2246; −0,0421; −0,0196). O valor analítico é (1,2249; −0,0421; −0,0196), igual dentro da precisão FP16: a transformação LittleCMS em float com `NOOPTIMIZE` não corta a gama.
- EXR com valor 4,0 é preservado.
- A orientação EXIF 6 é aplicada uma única vez.

**vcpkg:** versões e licenças na §5. Não existem *ports* de ImageMagick, kimageformats, libultrahdr nem resvg. O exiv2 é GPL-2.
