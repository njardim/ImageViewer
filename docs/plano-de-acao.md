# imageViewer — Plano de ação (v1.0 · 2026-10-06)

**Estado:** plano fechado. As três decisões estruturais foram tomadas a 2026-10-06: âmbito **SDR** na v1, produto **proprietário com distribuição direta**, stack **Python + PySide6**. Falta validar o plano para arrancar a Fase 0. Nenhum código da aplicação foi escrito.

**Origem de cada afirmação:**
- `[código]`: análise do código-fonte clonado (o commit está indicado em cada secção).
- `[doc]`: README ou documentação oficial.
- `[teste]`: medido por mim num contentor Linux x64 (Ubuntu 24.04, 4 vCPU, CPython 3.11) a 2026-10-06.
- `[conhecimento]`: conhecimento prévio que não verifiquei aqui.
- `[inferência]`: dedução a partir de evidência indireta.

Nada foi testado em Windows nem em macOS (ver §7.3).

---

## 1. Recomendação inicial

**Linguagem.** A linguagem tecnicamente ótima para um visualizador de imagens seria C++ com Qt 6, o caminho do qView. Dá o menor tempo de arranque e o menor tamanho, acesso direto às APIs de cor de cada sistema e os plugins KImageFormats.

Python é mesmo assim **adequado**, e a diferença pesa pouco neste produto. Em ambas as linguagens, a descodificação, a gestão de cor e a pintura correm em código nativo. Medi um executável PySide6 congelado com PyInstaller: abre uma fotografia de 1920×1280, mostra-a e termina em **0,21–0,28 s** `[teste]`.

A decisão é Python + PySide6, com critérios de abandono objetivos na Fase 0. O plano B é C++/Qt, com custo de migração contido: as classes Qt e a arquitetura são as mesmas, só muda a linguagem da camada de orquestração.

| Área | Decisão | Razão principal |
|---|---|---|
| Runtime | CPython 3.13 (recurso: 3.12) | Confirmar na Fase 0 que todas as wheels existem para cada plataforma-alvo. |
| Interface | PySide6 6.11 com Qt Widgets. A área de visualização é um widget próprio, pintado com `QPainter` sobre raster. | Controlo exato do píxel e testável por captura. Formatos 16 bits e float, `QColorSpace` com BT.2100 e `QRhiWidget` estão expostos em Python `[teste]`, o que deixa o HDR possível numa v2. |
| Descodificação | Registo de codecs com prioridades (o padrão do ImageGlass). Núcleo: libvips (`pyvips-binary`). Especialistas: `pi-heif` para HEIC, `imagecodecs` para JPEG XL e JPEG 2000, `rawpy`/LibRaw para RAW. Recurso para formatos de 8 bits: Qt e Pillow. | Nenhuma biblioteca Python cobre todos os formatos com fidelidade `[teste]`. |
| Gestão de cor | LittleCMS 2 através de `icc_transform` do libvips, a 16 bits, para o perfil do monitor obtido em cada sistema. | Suporta intenções, BPC e perfis LUT. O Qt serve de verificação cruzada: ΔE00 médio de 0,03 face ao LittleCMS `[teste]`. |
| Cor na v1 | Apenas SDR. Uma imagem HDR é mostrada como rendição SDR (tone mapping BT.2408) e a interface assinala-o. | Decisão tomada. |
| Empacotamento | PyInstaller 6.22 em modo onedir. Windows: Inno Setup com assinatura Authenticode. macOS: `.app` e DMG assinados e notarizados. Linux: AppImage e Flatpak. | Maturidade com PySide6. O pyvips-binary e o pi-heif congelaram sem hooks próprios `[teste]`. |
| Licenças | Só dependências LGPL ou permissivas. Excluem-se PyQt6 (GPL), pillow-heif (inclui o codificador x265, GPL), os packs GPL do LibRaw e o Ghostscript (AGPL). | Decisão tomada. |
| Magick.NET | Não usar. | Funciona a partir de Python, mas é dominado pelas alternativas (§4.4). |

**A premissa de que tudo depende.** O ImageGlass 10 já é multiplataforma, tem versão gratuita em GPL e lê mais de 90 formatos `[doc]`. O valor do imageViewer face a ele tem de vir de duas coisas: **fidelidade demonstrável** e **minimalismo**. A cobertura de formatos não diferencia.

Fidelidade demonstrável exige saber o que o compositor de cada sistema faz ao buffer que lhe entregamos:
- ColorSync no macOS;
- DWM com Auto Color Management no Windows 11;
- protocolo de gestão de cor no Wayland.

Nada disto está verificado. É o primeiro spike da Fase 0 e o maior risco (§7). Se falhar, o produto continua viável, mas a promessa reduz-se a "fidelidade até ao buffer entregue ao sistema".

---

## 2. Análise do qView

Base: jurplel/qView, commit `c5eca1c` (2026-04-04), versão 7.0, C++ com Qt 5/6, cerca de 6 500 linhas em `src/`. Tudo nesta secção é `[código]`, salvo indicação.

**Preservar**
- **Minimalismo por omissão.** Sem barra de menus (`menubarenabled=false`), fundo `#212121`, e a janela ajusta-se à imagem entre 20 % e 70 % do ecrã (`settingsmanager.cpp`).
- **Descodificação em segundo plano** com `QtConcurrent` e pré-carregamento dos vizinhos (distância 1 ou 4). A cache é limitada por memória (256 MB ou 2 GB) e a chave inclui o hash do perfil de destino, por isso invalida corretamente quando o perfil muda (`qvimagecore.cpp`).
- **Escala em dois tempos.** Durante o zoom aplica-se uma transformação rápida; depois de uma pausa, reamostra-se a partir do original à resolução física (`devicePixelRatioF`) (`qvgraphicsview.cpp`). É o padrão certo para ecrãs HiDPI.
- **Zoom ancorado no cursor**, com compensação do erro de arredondamento, zoom fracionário para trackpad e gesto de pinça.
- **Navegação na pasta:** ordenação natural (`QCollator` numérico), ciclo, e omissão dos ficheiros ocultos e dos `._*` do macOS.
- **Conversão para o perfil do monitor ligada por omissão** (`colorspaceconversion=1`). O perfil ICC obtém-se por sistema: `GetICMProfileW` no Windows, `NSWindow.colorSpace` no macOS, átomo `_ICC_PROFILE` no X11. Imagens sem perfil são tratadas como sRGB.
- **Associações não intrusivas.**
  - Windows: regista-se em `OpenWithProgids`, ou seja, aparece em "Abrir com" sem tomar o lugar da aplicação predefinida (`qView.iss`).
  - macOS: `CFBundleDocumentTypes` com papel *Viewer*, e o `QFileOpenEvent` é tratado (`qvapplication.cpp`).
  - Linux: `MimeType=` no ficheiro `.desktop`.

**Simplificar**
- Seis modos de ordenação (incluindo tipo MIME e aleatório) passam a três: nome natural, data de modificação e tamanho, com opção de ordem inversa.
- Seis diálogos (boas-vindas, sobre, atalhos, renomear, opções, informação) passam a um painel lateral de informação e uma janela de definições.
- "Abrir URL" e a verificação de atualizações pela rede saem da v1, por privacidade e para reduzir a superfície de ataque.
- Três parâmetros de redimensionamento da janela passam a um único comportamento com valores sensatos.

**Melhorar** (defeitos verificáveis no código)
- **Quantização prematura.** `readFile` converte para `Format_ARGB32_Premultiplied` (8 bits) *antes* de `convertToColorSpace`. Uma imagem de 16 bits já está quantizada quando a cor é convertida, o que perde precisão sem necessidade e pode produzir banding em gradientes. O novo pipeline converte a 16 bits e quantiza uma única vez.
- **Linux só suporta X11** para obter o perfil do monitor; não há caminho Wayland. Não verifiquei se reconverte a imagem quando a janela muda de ecrã.
- **Animações descodificadas duas vezes:** `QImageReader` para a 1.ª frame e `QMovie` para o resto, mais um contorno específico para APNG.
- **A rotação re-renderiza píxeis.** O próprio código diz "TODO: Remove this function---extremely inefficient". Deve ser uma transformação da vista.
- **SVG rasterizado uma única vez** à maior dimensão de ecrã, por isso fica desfocado acima desse zoom. Deve ser re-rasterizado consoante o zoom.
- **Formatos modernos** (AVIF, HEIF, JXL…) dependem de binários kimageformats descarregados durante o build (`download-plugins.ps1`). A cobertura deverá variar consoante o pacote `[inferência]`.

---

## 3. Análise do ImageGlass

Repositório oficial: d2phap/ImageGlass, commit `2cf91de` (2026-09-27). Analisei o código e não precisei de pedir ficheiros.

Há um facto que muda o enquadramento do pedido. **O ImageGlass 10 (pasta `source/`) foi reescrito sobre .NET 10, Avalonia/Skia e Magick.NET Q16-HDRI, e corre em:**
- Windows 10 1809+ (x64 e arm64);
- macOS 14+, só Apple Silicon;
- Linux X11 x64.

Fontes: ficheiros `.csproj` `[código]` e README `[doc]`. A v9 (WinForms, só Windows) continua na pasta `v9/`. A licença é dupla: "Classic" gratuita em GPLv3 e edições "Pro" pagas `[doc]`. Há cerca de 84 000 linhas de C# em `source/` `[teste]`.

**Preservar**
- **Registo de codecs com prioridades e seleção determinística** (`CodecRegistry.cs`) `[código]`. A ordem é SVG, depois Skia, e por fim Magick.NET como último recurso, que identifica o formato pelo conteúdo do ficheiro. Plugins podem sobrepor-se aos codecs nativos, e o codec vencedor fica em cache por extensão. É esta a espinha dorsal que proponho.
- **Profundidade preservada no caminho rápido.** O Skia mantém RGBA F16, 16161616 e 1010102, bem como o espaço de cor de origem (`SkiaCodec.cs`) `[código]`.
- **Opções de perfil de destino:** nenhum, perfil do monitor, perfis embutidos (sRGB, Adobe RGB, …) e ICC personalizado (`Photoing_Enums.cs`) `[código]`.
- **Rendição SDR de HDR com operador documentado** (`HdrToneMappingOptions.cs`) `[código]`:
  - BT.2408 por omissão, com branco de referência a 203 nits;
  - controlo de exposição;
  - alternativas Reinhard e ACES.
- **Ferramentas de visualização** `[código: comandos IG_*]`:
  - modos de zoom: ajustar, preencher, largura, altura, bloquear;
  - fundo em xadrez;
  - vista por canal R/G/B/A;
  - recarregamento quando o ficheiro muda;
  - modos sem moldura e de ajuste da janela.
- **"Definir como predefinido" sem truques.**
  - No Windows abre `ms-settings:defaultapps` para o utilizador escolher `[código]`.
  - Os ativos de empacotamento incluem `Info.plist` com `CFBundleDocumentTypes`/`LSHandlerRank`, ficheiros `.desktop` com os tipos MIME de RAW, MSI via WiX, AppImage e Flatpak `[código: source/__assets]`.

**Simplificar**
- Cerca de 150 identificadores `IG_*` (comandos e pincéis de tema) `[teste]` passam a cerca de 35 ações.
- 31 ficheiros de páginas de definições `[teste]` passam a uma janela com quatro secções.
- Temas, layouts, barra de ferramentas e atalhos personalizáveis passam a um tema automático claro/escuro e atalhos fixos na v1.
- Saem da v1:
  - ferramentas de edição: recortar, redimensionar, compressão sem perdas;
  - definir como fundo do ambiente de trabalho ou do ecrã de bloqueio;
  - imprimir e partilhar;
  - SDK de plugins e ferramentas externas.

  Fica apenas "Abrir com…".

**Melhorar**
- **Gestão de cor ligada para todos.** No assistente inicial, só quem escolhe o perfil "profissional" fica com o perfil do monitor; os restantes ficam sem gestão de cor (`QuickSetupWindow.cs:218-220`) `[código]`.
- **Perfil do monitor nos três sistemas.** O fornecedor de perfil só existe para Windows (`Win32ColorProfileProvider`, que também deteta HDR via DXGI). Em macOS e Linux não há nenhum: `Core.ColorProfileProvider` só é atribuído em `MainWindow32.cs` `[código]`. Não verifiquei se o Avalonia delega no ColorSync em macOS.
- **Desempenho do recurso de último nível.** Na mesma imagem de 24 MP, o ImageMagick (via Magick.NET) levou 1,8–2,2 s só a descodificar. O libvips levou 0,57–0,87 s já incluindo a cópia para numpy `[teste, imagem sintética]`.
- **Plataformas que o ImageGlass 10 não declara suportar:** sessões Wayland e Mac com processador Intel `[doc]`.

---

## 4. Comparação tecnológica

### 4.1 Ler um formato não é o mesmo que apresentá-lo corretamente

Um descodificador que devolve píxeis não garante uma apresentação correta. Para isso é preciso:
1. interpretar as etiquetas de cor (ICC, nclx/CICP em AVIF/HEIF, `gAMA`/`cHRM`/`cICP` em PNG, codificação enumerada em JXL);
2. aplicar a orientação uma única vez;
3. tratar corretamente a semântica do alfa (direto ou pré-multiplicado);
4. preservar a profundidade de bits até ao fim;
5. aplicar a transformação para o ecrã;
6. reamostrar bem ao fazer zoom;
7. converter HDR para SDR quando aplicável.

Exemplos medidos ou observados:
- O Pillow 12.3 *lê* PNG RGB de 16 bits, mas *entrega* 8 bits (256 níveis em vez de 1 024 numa rampa) `[teste]`.
- O qView *lê* 16 bits, mas quantiza para 8 bits antes de converter a cor `[código]`.
- O pyvips *lê* AVIF PQ, mas sem tone mapping o resultado num ecrã SDR fica errado.
- O pillow-jxl-plugin *lê* JXL de 16 bits como RGB de 8 bits `[teste]`.

### 4.2 Interface gráfica em Python

| Opção | Vantagens | Limitações | Veredicto |
|---|---|---|---|
| **PySide6 6.11 (Qt Widgets)** | Licença LGPLv3 `[teste: metadados]`. `QImage` em RGBA64 e FP16/FP32. `QColorSpace` com Display P3, Rec.2020 e BT.2100 PQ/HLG. `QRhi`/`QRhiWidget` expostos em Python. A biblioteca Wayland do Qt inclui `wp_color_manager_v1` `[teste]`. Importação em 101 ms `[teste]`. | Pacote pesado (99 MB num bundle congelado) `[teste]`. A wheel não lê HEIC, AVIF nem JXL `[teste]`. | **Recomendado** |
| PyQt6 | O mesmo Qt. | GPL ou licença comercial paga. | Rejeitado (licença) |
| Qt Quick/QML | Animações fluidas. | Menos controlo direto do píxel e mais difícil de testar ao píxel. | Rejeitado na v1 |
| wxPython | Widgets nativos. | Pipeline de imagem de 8 bits, sem gestão de cor `[conhecimento]`. | Rejeitado |
| Tkinter | Vem com o Python. | Sem HiDPI sério e sem gestão de cor. | Rejeitado |
| Flet / pywebview | Aspeto web moderno. | Depende de um motor web, sem caminho de 16 bits, gestão de cor heterogénea. | Rejeitado |
| Kivy / Dear PyGui | Renderização GPU. | Experiência não nativa, sem ICC. | Rejeitado |

### 4.3 Descodificação e gestão de cor

| Biblioteca (versão testada) | Formatos relevantes | Profundidade e cor | Licença | Papel |
|---|---|---|---|---|
| **pyvips 3.2.0 + pyvips-binary 8.18.7** | Lê JPEG, PNG, WebP (animado), TIFF, GIF, AVIF, SVG, UltraHDR. **Não lê** HEIC, JXL, JPEG 2000, EXR, RAW, PDF, nem tem o recurso ImageMagick `[teste]`. | Preserva 16 bits (1 024 níveis) `[teste]`. `icc_transform` (LittleCMS) disponível `[teste]`. | MIT; o libvips incluído é LGPL-3.0+ `[teste]` | **Núcleo**, também do pipeline de cor |
| **pi-heif 1.4.0** | HEIC/HEIF: libheif 1.23 e libde265, **sem codificador** `[teste: wheel Linux]` | 8 a 12 bits | BSD-3; bibliotecas LGPL | HEIC (depende da decisão D3) |
| pillow-heif 1.8.0 | Os mesmos formatos, mas inclui **x265 (GPL)** `[teste]` | — | BSD-3 com componente GPL incluído | Rejeitado (licença) |
| **imagecodecs 2026.3.6** | JXL (libjxl 0.11.2), JPEG 2000, JPEG XR, QOI. Sem HEIF `[teste]`. | JXL de 16 bits sem perdas reproduz os dados exatamente `[teste]` | BSD-3 | JXL e JPEG 2000. Ocupa 80 MB instalado `[teste]`, por isso tem de ser podado. |
| **rawpy 0.27.1** (LibRaw 0.22.1) | RAW de câmara. Construído **sem** os packs GPL de demosaico `[teste]`. | 16 bits | MIT; LibRaw em LGPL/CDDL | RAW. Tem hook PyInstaller `[teste]`. |
| OpenImageIO 3.1.18.1 | EXR, DPX, Cineon, HDR, DDS, PSD, TGA, FITS. Sem HEIF, AVIF, RAW nem JXL na wheel `[teste]`. | Float. Inclui configuração OCIO embutida `[teste]`. | Apache-2.0 | Opcional na v1.1 (EXR/DPX) |
| Pillow 12.3.0 | Muitos formatos de 8 bits, mais AVIF e JPEG 2000 | **Trunca RGB de 16 bits para 8 bits** `[teste]` | MIT-CMU | Recurso só para formatos de 8 bits |
| Qt `QImageReader` (wheel PySide6) | BMP, CUR, GIF, ICNS, ICO, JPEG, PNG, PBM/PGM/PPM, SVG, TGA, TIFF, WBMP, WebP, XBM, XPM `[teste]` | PNG de 16 bits é lido como RGBX64 `[teste]` | LGPL | Recurso para ICO, ICNS e CUR |
| Wand 0.7.2 (ImageMagick) | Cobertura muito ampla | Q16/HDRI | MIT; ImageMagick próprio | Rejeitado. Exige ImageMagick instalado no sistema; não encontrei wheel que o inclua `[não confirmado exaustivamente]`. |
| Magick.NET 14.17.2 via pythonnet | 257 entradas legíveis `[teste]` | Q16-HDRI, 16 bits preservados `[teste]` | Apache-2.0 | Rejeitado (§4.4) |

**Desempenho indicativo** `[teste]`: imagem sintética de ruído 6000×4000 (pior caso para compressão), 4 vCPU, uma corrida, tempo até ter os píxeis num array numpy.

| Caminho | JPEG 8 bits | PNG 16 bits |
|---|---|---|
| pyvips | 569 ms (uint8) | 865 ms (uint16) |
| Pillow | 655 ms | 1 428 ms (**truncado para 8 bits**) |
| OpenImageIO | 708 ms (float32) | 1 185 ms (float32) |
| Magick.NET via pythonnet | 1 756 + 1 442 + 235 = **3 433 ms** | 2 161 + 1 415 + 286 = **3 862 ms** |

As três parcelas do Magick.NET são, por esta ordem, descodificação, `ToShortArray` e cópia para numpy.

**Motor de cor** `[teste]`: P3 → sRGB, intenção relativa, 16 bits. O Qt `QColorSpace` e o LittleCMS diferem em ΔE00 médio 0,034, percentil 99 de 0,37 e máximo de 0,72. O teste só cobre perfis matriz/TRC. Escolho o LittleCMS como motor principal porque suporta perfis LUT, todas as intenções e BPC. O Qt fica como verificação cruzada.

### 4.4 Magick.NET: verificação e veredicto

**Formatos.** O README diz "over 100 major file formats (not including sub-formats)" `[doc]`. A afirmação "mais de 90" é do ImageGlass, não do Magick.NET `[doc]`. A versão 14.17.2 lista 257 entradas legíveis, mas incluem pseudo-formatos como gradient, xc, label e plasma `[teste]`. Os "100+ formatos principais" são plausíveis.

**Variantes Q8, Q16 e Q16-HDRI.** Confirmadas, para x64, arm64 e x86, em versões normal, OpenMP e AnyCPU `[doc]`.
- **Q8:** precisão interna de 8 bits. Trunca fontes de 16 bits, portanto é inadequada para fidelidade.
- **Q16:** inteiro de 16 bits.
- **HDRI:** vírgula flutuante; admite valores fora de intervalo, necessários para HDR e EXR `[conhecimento]`.

A build testada reporta "ImageMagick 7.1.2-32 Q16-HDRI". Inclui os delegados heic, jxl, jp2, openexr, raw, lcms, rsvg, webp, tiff, entre outros `[teste]`.

**Plataformas e alvos .NET.** O pacote NuGet 14.17.2 tem binários para net8.0 e netstandard2.0, e nativos para linux-x64, linux-musl-x64, osx-x64 e win-x64 `[teste]`. O README do ramo principal (2026-10-05) já indica net10.0 e **macOS só arm64** `[doc]`. É provável que a próxima versão deixe de suportar Mac Intel `[inferência]`. A biblioteca nativa ocupa 25–41 MB por plataforma `[teste]`.

**Viabilidade em Python.** Funciona: pythonnet 3.2.0 com clr_loader 0.3.1 sobre CoreCLR (.NET 8.0.31), em Linux x64 `[teste]`. Os custos medidos e esperados são:
1. Um segundo runtime gerido no mesmo processo, que tem de ser distribuído, assinado e notarizado. No macOS, o CoreCLR com hardened runtime exige provavelmente entitlements de JIT `[conhecimento, não testado]`.
2. Mais 372 ms no arranque `[teste]`.
3. Ler a imagem é 3 a 4 vezes mais lento que com o libvips, contando com a passagem dos píxeis do .NET para Python `[teste]`.
4. Uma superfície de ataque grande: o ImageMagick tem um histórico de vulnerabilidades, como o ImageTragick de 2016 `[conhecimento]`.

**Veredicto `[confiança alta]`.** O valor do Magick.NET está no ImageMagick, não no .NET. Em Python, o mesmo motor estaria disponível sem o CLR, e a cobertura que importa obtém-se combinando bibliotecas, com melhor desempenho. O Magick.NET só seria a escolha natural numa aplicação .NET, que é precisamente a rota do ImageGlass 10. Seguir essa rota seria competir com ele na mesma stack.

### 4.5 Empacotamento

| Ferramenta | Vantagens | Limitações | Veredicto |
|---|---|---|---|
| **PyInstaller 6.22.3 (onedir)** | Maduro com PySide6. Existem hooks para rawpy e clr. O pyvips-binary e o pi-heif congelaram sem hooks próprios `[teste]`. `BUNDLE` aceita `info_plist`, `codesign_identity` e `entitlements_file` `[teste: código-fonte]`. O protótipo ocupou 148 MB sem otimização `[teste]`. | Não gera instaladores, por isso é preciso Inno Setup ou WiX, um DMG e AppImage/Flatpak à parte. O bootloader tem histórico de falsos positivos de antivírus no Windows `[conhecimento]`. | **Recomendado** |
| Briefcase 0.4.5 | `document_type` gera as associações nos três sistemas `[teste: código-fonte]`. Produz MSI, DMG, AppImage e Flatpak com uma só ferramenta. | Ecossistema menor para wheels nativas complexas e menos controlo. | Plano B |
| Nuitka / `pyside6-deploy` | Compila para C: arranque e proteção do código. | Builds longos; algumas funcionalidades são comerciais. | Reserva, se for preciso proteger o código ou se o arranque no Windows falhar |

---

## 5. Arquitetura e interface

### 5.1 Componentes

```
imageviewer/
  app/        Shell Qt: MainWindow, ViewerWidget (raster, DPR-correto), OverlayBar,
              InfoPanel, SettingsDialog, ações e atalhos, i18n (pt-PT, en-US)
  core/       Sem widgets: FolderModel (listagem, ordenação natural, file watcher),
              Preloader/Cache (limite de memória), ViewState (zoom, pan, rotação de vista)
  imaging/    Sem Qt: CodecRegistry + adaptadores (Vips, Heif, Jxl/J2K, Raw, Qt, Pillow),
              DecodedImage (píxeis, profundidade, alfa, ICC/CICP, orientação, frames, metadados),
              ColorPipeline (LittleCMS via libvips), Resampler, ToneMapper (HDR→SDR)
  platform/   Interface PlatformServices + win.py / mac.py / linux.py (ctypes):
              ICC do monitor e eventos de mudança, abertura de ficheiros, lixo,
              mostrar no gestor de ficheiros, abrir com, abrir definições de apps predefinidas
packaging/    spec PyInstaller, Inno Setup, Info.plist, .desktop + AppStream, manifesto Flatpak,
              scripts de assinatura e notarização
tests/        unitários, fidelidade, corpus de formatos, E2E por plataforma
```

**Regras de dependência:**
- `imaging` e `core` não importam widgets, e são testáveis sem ecrã.
- `platform` é o único sítio com código específico de cada sistema; o resto da base é comum.
- Mover ficheiros para o lixo usa `QFile.moveToTrash`, que funciona nos três sistemas `[conhecimento]`. Isto reduz o código por plataforma.

### 5.2 Pipeline de fidelidade

1. **Identificação do formato** pelos bytes iniciais, não pela extensão. O codec de maior prioridade capaz de ler o ficheiro descodifica-o; se falhar, passa ao seguinte.
2. **Descodificação na profundidade nativa** (uint8, uint16 ou float32), com alfa direto, mais as etiquetas de cor:
   - ICC embutido; ou
   - CICP/nclx, gAMA/cHRM ou codificação enumerada do JXL, a partir das quais se gera um perfil ICC equivalente; ou
   - nenhuma etiqueta, caso em que se assume sRGB e a interface mostra-o.
3. **Orientação aplicada exatamente uma vez** (EXIF, TIFF, HEIF `irot`/`imir`). Cada descodificador declara se já a aplicou, o que evita a dupla rotação típica de HEIC.
4. **Imagens HDR** (PQ, HLG, gain map, float):
   - UltraHDR com gain map: usa-se a imagem base SDR;
   - PQ e HLG: tone mapping BT.2408 com branco de referência a 203 nits;
   - em todos os casos a interface assinala "HDR → SDR".
5. **Transformação para o perfil do ecrã** com LittleCMS, a 16 bits (ou float para entradas float). Por omissão usa intenção colorimétrica relativa com compensação de ponto negro (BPC). A cor é convertida sobre valores não pré-multiplicados; o alfa é pré-multiplicado depois.
6. **Cache referida ao ecrã:**
   - imagem atual em RGBA de 16 bits pré-multiplicado, com resolução total;
   - vizinhos guardados como pré-visualizações ao tamanho do ecrã, geradas por *shrink-on-load*;
   - a chave inclui ficheiro, data de modificação, tamanho, hash do perfil de ecrã e intenção.

   Um JPEG de 24 MP ocupa 192 MB em 16 bits; por isso os vizinhos não são guardados em resolução total.
7. **Pintura:**
   - a 100 %, 1 píxel da imagem corresponde a 1 píxel físico (respeitando o `devicePixelRatio`);
   - abaixo de 100 %, mostra-se uma pré-visualização rápida e, depois de uma pausa, uma reamostragem Lanczos3 a partir da resolução total, calculada noutro thread (o padrão do qView);
   - a partir de 200 % usa-se vizinho mais próximo; entre 100 % e 200 %, interpolação bilinear.
8. **Quantização final** para a profundidade do buffer da janela, uma única vez. Normalmente são 8 bits `[conhecimento]`.

### 5.3 Critérios de fidelidade verificáveis

| ID | Critério | Método e limiar de aceitação |
|---|---|---|
| F1 | Descodificação | **Formatos sem perdas** (PNG, TIFF, WebP sem perdas, JXL sem perdas, BMP, QOI): buffer idêntico, bit a bit, ao de um descodificador de referência, na profundidade nativa. **Formatos com perdas** (JPEG, AVIF, HEIC, WebP): bit a bit idêntico à mesma biblioteca de referência, ou PSNR ≥ 50 dB face a um segundo descodificador independente. Implementações diferentes da IDCT podem diferir legitimamente ±1. |
| F2 | Profundidade de bits | Uma rampa de 16 bits atravessa o pipeline sem quantização intermédia: ≥ 1 024 níveis distintos num alvo de captura de 16 bits. Num alvo de 8 bits, erro ≤ 1 LSB face a um pipeline de referência em float. |
| F3 | Gestão de cor | Corpus com sRGB, Display P3, Adobe RGB, ProPhoto, Rec.2020 SDR, cinzento γ2.2, CMYK FOGRA39, ICC v2 e v4, perfis LUT e AVIF/HEIC com CICP. Destinos: perfis sintéticos de ecrã sRGB, P3 e de gama larga. Limiares face a uma referência **independente** (ArgyllCMS `cctiff` e colour-science): ΔE00 médio ≤ 0,5, percentil 99 ≤ 1,0, máximo ≤ 2,0. Exceções fora de gama documentadas por intenção. |
| F4 | Imagens sem perfil | São tratadas como sRGB e o painel de informação diz "perfil assumido". |
| F5 | Perfil do monitor | Obtido nos três sistemas. Mover a janela para um ecrã com outro perfil, ou mudar o perfil no sistema, leva a reconversão em ≤ 500 ms. |
| F6 | Transparência | Composição sobre xadrez ou cor sólida com erro ≤ 1 LSB face a uma composição de referência. Sem halos escuros numa imagem de teste com gradiente de alfa. |
| F7 | Orientação | As orientações EXIF 1 a 8 aparecem corretas em JPEG, TIFF, HEIC, AVIF, WebP, JXL, PNG `eXIf` e RAW, sem dupla aplicação. |
| F8 | Escala | **A 100 %:** captura idêntica bit a bit à referência com DPR 1; 1,25; 1,5 e 2. **Redução:** ΔE00 médio ≤ 1 face a uma redução Lanczos3 do libvips, sem aliasing visível numa imagem de teste com padrão de zonas. **Ampliação ≥ 200 %:** cada píxel da imagem forma um bloco exato. |
| F9 | Animação | Atrasos entre frames cumpridos com margem de ±10 ms. A regra de mínimo dos browsers para atrasos de GIF abaixo de 20 ms fica documentada. Disposição e mistura de frames corretas; gestão de cor aplicada a cada frame. |
| F10 | Metadados de cor | O perfil, a profundidade e as etiquetas mostrados no painel coincidem com o ficheiro. |
| F11 | HDR → SDR | A imagem base de um UltraHDR é mostrada sem alterações. PQ e HLG seguem o operador documentado. A interface indica que houve rendição. |
| F12 | Sem degradação silenciosa | Qualquer perda fica visível no painel de informação: perfil não suportado, CMYK sem perfil, recurso para um descodificador de 8 bits. |

### 5.4 Limites que a aplicação não controla

Estes limites não se resolvem no código e a aplicação não promete o contrário:
- **Buffer de 8 bits.** O buffer final é de 8 bits na maioria dos caminhos SDR, o que implica um erro de quantização de até 1 LSB. Saída a 10 bits fica fora da v1.
- **Gama do ecrã.** Cores fora da gama do monitor são cortadas ou mapeadas segundo a intenção. Um ecrã sRGB não mostra cores P3.
- **Qualidade do perfil.** A precisão depende da calibração do monitor; um perfil EDID genérico é aproximado.
- **Compositor do sistema.** O que o sistema faz depois de receber o buffer pode alterá-lo:
  - **macOS:** não sei, e não consegui verificar aqui, se o Qt 6.11 marca a superfície da janela com o espaço de cor do ecrã ou com sRGB. No primeiro caso devemos converter para o perfil do monitor, como faz o qView; no segundo caso a conversão é feita pelo ColorSync e não a devemos fazer nós.
  - **Windows 11 com ACM:** quando ativo, o DWM gere a cor e trata as aplicações como sRGB `[conhecimento, não verificado]`. Converter para o perfil do monitor causaria dupla conversão.
  - **Wayland:** a gestão de cor depende de o compositor suportar o protocolo.

  Por isso, a fidelidade garantida termina no buffer entregue ao sistema. O que se vê no ecrã é verificado por sistema na Fase 0 e na Fase 5.
- **Ajustes de hardware e de sistema.** Dithering da GPU, FRC do painel, Night Light e True Tone alteram o resultado e estão fora de controlo.

### 5.5 Organização multiplataforma

| Aspeto | Windows | macOS | Linux |
|---|---|---|---|
| Perfil do monitor | `GetICMProfileW` por monitor (ctypes) e deteção do ACM | ICC do ColorSync via CoreGraphics (ctypes, sem pyobjc) | Átomo X11 `_ICC_PROFILE[_n]`; colord via D-Bus; Wayland `wp_color_management_v1` (presente no Qt 6.11 `[teste]`) |
| Ficheiro aberto por associação | `argv` (`"%1"`) | `QFileOpenEvent` (Apple Event), **não** `argv` | `argv` (`%F`) |
| Registo da associação | O instalador regista um ProgID, `OpenWithProgids` e Capabilities. Quem escolhe a aplicação predefinida é o utilizador (`ms-settings:defaultapps`): desde o Windows 10, uma aplicação não se pode definir sozinha como predefinida `[conhecimento]`. | `CFBundleDocumentTypes` e UTIs, `LSHandlerRank` em `Alternate`. O utilizador define a predefinida em "Obter informações → Alterar tudo". | Ficheiro `.desktop` com `MimeType` e metadados AppStream; `xdg-mime default` fica com o utilizador |
| Instância única | `QLocalServer` (named pipe) | O sistema entrega os ficheiros à instância já aberta | `QLocalServer` (socket Unix) |
| Distribuição | Inno Setup com assinatura Authenticode | `.app` + DMG com Developer ID e notarização | AppImage (principal) e Flatpak (Flathub) |

### 5.6 Experiência de utilização

**Princípios:**
- A imagem é a interface: não há barra de ferramentas permanente.
- Os controlos aparecem quando o rato se move e desaparecem 1,5 s depois. Respeita-se a opção do sistema para reduzir movimento.
- O teclado chega para tudo.
- Não há diálogos modais no fluxo normal.
- Cada controlo visível corresponde a uma das 10 tarefas mais frequentes.

**Janela:**
- Barra de título nativa com "nome do ficheiro — 3/57".
- Fundo neutro escuro ou claro, conforme o tema do sistema; pode ser xadrez ou uma cor escolhida.
- Fonte do sistema e ícones de linha (Lucide, licença ISC).
- Grelha de espaçamento de 8 px e transições de 120 ms.

**Barra flutuante inferior** (aparece ao mover o rato):
- anterior e seguinte (também há zonas clicáveis nas margens laterais);
- nome do ficheiro e posição na pasta;
- percentagem de zoom (um clique alterna entre ajustar e 100 %);
- rodar a vista;
- informação;
- ecrã inteiro.

**Painel de informação** (tecla `I`):
- ficheiro, dimensões, formato e codec usado;
- profundidade de bits e alfa;
- **cadeia de cor:** perfil de origem (ou "sRGB assumido") → perfil do ecrã → intenção; indicação de HDR → SDR quando aplicável;
- EXIF essencial.

Esta transparência sobre o pipeline de cor é o fator diferenciador visível.

**Menu de contexto** (é a fonte única de ações em Windows e Linux; no macOS há também barra de menus nativa, como manda a HIG):
- Abrir…, Recentes ▸, Abrir com ▸, Mostrar na pasta
- Copiar imagem, Copiar caminho
- Rodar ↻/↺, Espelhar
- Zoom ▸ (Ajustar, Preencher, 100 %, Largura)
- Fundo ▸
- Apresentação
- Mudar nome, Mover para o lixo
- Informação, Definições

**Atalhos:**

| Tecla | Ação |
|---|---|
| ←/→ ou PgUp/PgDn | Imagem anterior / seguinte |
| Home / End | Primeira / última imagem |
| `+` / `−` / roda | Zoom |
| `0` | Ajustar à janela |
| `1` | 100 % (píxeis reais) |
| F, F11 ou duplo clique | Ecrã inteiro (Esc sai) |
| R / Shift+R | Rodar a vista ↻ / ↺ |
| H | Espelhar |
| I | Painel de informação |
| Del | Mover para o lixo (Ctrl/⌘+Z desfaz) |
| F2 | Mudar nome |
| Ctrl/⌘+C | Copiar imagem |
| Ctrl/⌘+Shift+C | Copiar caminho |
| Ctrl/⌘+O | Abrir |
| K | Pausar / continuar animação |
| `,` / `.` | Frame anterior / seguinte |
| S | Apresentação |
| Ctrl/⌘+, | Definições |

**Definições** (cerca de 10, em quatro secções):
- **Aspeto:** tema; fundo.
- **Cor:** perfil de destino (automático, sRGB, ICC personalizado, desativado para diagnóstico); intenção (relativa com BPC, ou percetual).
- **Navegação:** comportamento da roda; ordenação; ciclo na pasta; reprodução automática de animações.
- **Sistema:** limite de memória do pré-carregamento; confirmar eliminação; botão "Associações…", que abre as definições do sistema.

**Estado vazio:** "Arraste uma imagem ou prima Ctrl/⌘+O", com a lista de recentes.

**Erros:** mostrados na própria janela, sem diálogos, com o motivo da falha; a navegação continua a funcionar.

**Acessibilidade:** tudo acessível pelo teclado, nomes acessíveis nos controlos, contraste ≥ 4,5:1 nas sobreposições.

---

## 6. Plano de execução

### 6.1 Âmbito

**Essencial na v1:**
- **E1 Abrir:** associações, `argv`/`QFileOpenEvent`, diálogo de abertura, arrastar e largar, recentes.
- **E2 Navegar na pasta:** ordenação natural, ciclo, *file watcher*, pré-carregamento.
- **E3 Formatos:**
  - JPEG (incluindo UltraHDR), PNG e APNG, GIF, WebP, AVIF, HEIC/HEIF, JPEG XL, TIFF multipágina;
  - BMP, ICO/CUR, ICNS, SVG, TGA, PNM, QOI, JPEG 2000;
  - RAW: DNG, CR2/CR3, NEF, ARW, RAF, ORF, RW2. Mostra primeiro a pré-visualização embutida e faz o demosaico completo a pedido.
- **E4 Fidelidade:** o pipeline completo, cumprindo F1 a F12.
- **E5 Zoom e pan:** todos os modos, gestos de trackpad, rotação e espelho da vista (sem alterar o ficheiro).
- **E6 Animação:** reprodução, pausa e avanço frame a frame.
- **E7 Visualização:** ecrã inteiro e apresentação.
- **E8 Painel de informação.**
- **E9 Ações sobre ficheiros:** mover para o lixo com desfazer, mudar nome, copiar, mostrar na pasta, abrir com.
- **E10 Integração com o sistema:** associações e instância única nos três sistemas.
- **E11 Definições e idiomas:** definições mínimas, tema automático, pt-PT e en-US.
- **E12 Distribuição:** pacotes assinados e CI.

**Opcional, por ordem de valor:**
- **O1** EXR, DPX e HDR via OpenImageIO + OCIO (relevante para a Cristallumnis).
- **O2** Tira de miniaturas com cache em disco.
- **O3** Inspetor de píxel (valor de origem vs valor apresentado), vista por canal e histograma.
- **O4** Comparação A/B (sinergia com o upscaling da Cristallumnis, como plugin fora do núcleo).
- **O5** HDR real com `QRhiWidget` (EDR, scRGB, HDR em Wayland).
- **O6** Saída a 10 bits.
- **O7** Atalhos remapeáveis.

**Fora do âmbito:** edição e gravação de imagens, impressão, fundos de ambiente de trabalho, nuvem, SDK de plugins, telemetria.

### 6.2 Fases

| Fase | Entregáveis | Depende de | Critérios de aceitação | Esforço |
|---|---|---|---|---|
| **0. Validação técnica** | **Estrutura do repositório:** pyproject, lockfile, ruff, mypy, pytest, CI com Windows, macOS arm64/x64 e Linux. **S1:** visualizador mínimo congelado nos três sistemas. **S2:** cadeia de apresentação por sistema — mostrar valores conhecidos e capturar a saída do compositor (screencapture, Desktop Duplication, screenshot por portal). **S3:** matriz de wheels por Python e arquitetura, inventário de licenças, poda do imagecodecs. **S4:** responsividade da UI e libertação do GIL durante descodificações de 24 MP e 100 MP. **S5:** parecer jurídico sobre HEVC e plano de cumprimento LGPL. | — | **Go/no-go do Python:** (a) arranque a frio até à 1.ª imagem (JPEG 12 MP) com p50 ≤ 700 ms em Windows 11 e Mac de referência, e ≤ 400 ms a quente; (b) instalador ≤ 120 MB, instalação ≤ 350 MB; (c) 0 deteções no Defender e ≤ 2 no VirusTotal depois de assinar; (d) notarização aceite e Gatekeeper sem avisos; (e) tempo de frame p95 ≤ 16,7 ms durante descodificação. **Decisão:** se (a) ou (c) falharem após mitigação, testar Nuitka; se continuar a falhar, mudar para C++/Qt. O S2 decide a estratégia de cor em cada sistema. | M |
| **1. Núcleo de imagem** | `DecodedImage`, `CodecRegistry` e os adaptadores, ColorPipeline, orientação, ToneMapper, harness de fidelidade, corpus. | 0 | F1–F4, F6, F7, F10–F12 em CI nos três sistemas, sem ecrã. | L |
| **2. Visualizador mínimo** | Janela, ViewerWidget (escala em dois tempos, correto em DPR), zoom e pan, navegação, pré-carregamento, *file watcher*, abrir e arrastar. | 1 | F8. Imagem seguinte já pré-carregada mostrada em ≤ 50 ms (p95). JPEG de 24 MP não pré-carregado: pré-visualização ≤ 150 ms e nítida ≤ 400 ms. PNG de 100 MP abre sem exceder o limite de memória. A RAM cresce ≤ 10 % ao fim de 1 000 navegações. | M |
| **3. Integração com o sistema** | PlatformServices (perfil do monitor e respetivos eventos, lixo, mostrar na pasta, abrir com), instância única, `QFileOpenEvent`. | 2 | F5. Ações de ficheiro reversíveis e testadas em cada sistema. | M |
| **4. Interface completa** | Sobreposições, menu de contexto, painel de informação, definições, temas, animação (F9), apresentação, ecrã inteiro, i18n, acessibilidade, estados vazio e de erro. | 2, 3 | F9. Teste com 5 a 8 utilizadores em 8 tarefas (abrir a partir do sistema, navegar, ver a 100 %, ecrã inteiro, ver a cadeia de cor, apagar e desfazer, rodar a vista, mostrar na pasta): ≥ 90 % de sucesso sem ajuda. | L |
| **5. Empacotamento e associações** | Specs PyInstaller, Inno Setup, Info.plist, DMG e notarização, AppImage e Flatpak, assinatura em CI. | 3 (e o pipeline da Fase 0) | **E2E em cada sistema:** instalar → abrir por duplo clique, `open`/`start`/`xdg-open` → abre aquele ficheiro. Um segundo ficheiro reutiliza a instância. Desinstalar limpa o registo, o LaunchServices e os ficheiros `.desktop`. **Matriz:** Windows 11 (e Windows 10 22H2 se D2 o exigir); as três últimas versões principais do macOS; Ubuntu LTS com GNOME/Wayland, KDE Plasma 6/Wayland e uma sessão X11. | M |
| **6. Endurecimento e beta** | Fuzzing dos descodificadores com ficheiros corrompidos, profiling, procura de fugas de memória, beta com 10 a 20 utilizadores, release candidate. | 4, 5 | Nenhum crash num corpus de mais de 10 000 ficheiros corrompidos (ou crash isolado e recuperado). Nenhum bug P1 em aberto. F1–F12 verdes nos três sistemas. | M |
| **7. Pós-v1** | O1 a O7, por ordem de valor. | 6 | Critérios definidos por funcionalidade. | — |

**Estimativa:** 14 a 20 semanas para um engenheiro sénior com assistência de IA, até à v1 `[estimativa; confiança baixa]`. O que mais pode alterar este número é o resultado do spike S2 (cadeia de apresentação) e a quantidade de casos especiais de cor em cada sistema.

### 6.3 Estratégia de testes

**Corpus de testes:**
- Imagens sintéticas geradas pelo próprio projeto:
  - rampas de 8 e 16 bits;
  - patches em P3, Adobe RGB e ProPhoto;
  - gradientes de alfa;
  - padrão de zonas;
  - conjunto das 8 orientações em cada formato.
- Fontes públicas (licenças a confirmar):
  - PngSuite;
  - exif-orientation-examples;
  - conformidade do libjxl;
  - testes do libavif e do libheif;
  - imagens de teste ICC v2/v4 (color.org);
  - raw.pixls.us.

**Referência independente.** A verificação não pode ser circular: se o produto usa LittleCMS através do libvips, a referência não pode usar o mesmo motor. Usam-se o ArgyllCMS `cctiff`, que tem motor próprio, e o colour-science para perfis matriz/TRC.

**Verificação no ecrã.** Protocolo manual em cada sistema, com colorímetro e ArgyllCMS `spotread`, num monitor de referência. Os patches mostrados devem ficar a ΔE00 ≤ 2 do valor previsto. Este limiar é imposto pelo próprio ecrã e pelo perfil.

**CI.** Testes unitários e de fidelidade sem ecrã nos três sistemas; testes E2E de associações em runners próprios ou em máquinas virtuais. Cada pull request corre F1–F4 e F6–F8.

---

## 7. Riscos e decisões pendentes

### 7.1 Riscos

| Risco | Prob. | Impacto | Mitigação |
|---|---|---|---|
| R1. A cadeia de apresentação de algum sistema impede controlar ou conhecer a conversão de cor (dupla conversão ou nenhuma) | Média | Alto | Spike S2. Estratégia de cor por sistema atrás da interface `platform`. Na pior hipótese, a promessa reduz-se a "fidelidade até ao buffer". |
| R2. Arranque lento ou falsos positivos de antivírus no Windows (bootloader do PyInstaller) | Média | Alto | Assinatura, modo onedir, bootloader compilado de raiz, submissão à Microsoft. Critério go/no-go → Nuitka → C++/Qt. |
| R3. Faltam wheels para alguma arquitetura (win-arm64, mac-x64) | Média | Médio | S3. Reduzir a matriz (D1) ou compilar a wheel em falta. |
| R4. Patentes HEVC ao distribuir o libde265 | Média | Médio–Alto | Parecer jurídico (D3). Alternativa: codecs do sistema (HEIF via WIC no Windows, ImageIO no macOS) atrás da mesma interface de codec. |
| R5. Superfície de ataque dos parsers (libvips, libheif, LibRaw, libjxl, libtiff) | Média | Alto | Dependências atualizadas automaticamente, fuzzing, opção de isolar RAW e TIFF exótico num processo separado, nenhuma funcionalidade de rede. |
| R6. Incumprimento de licenças | Baixa | Alto | Só LGPL com ligação dinâmica e avisos de licença no instalador. Lista negra: PyQt6, pillow-heif, packs GPL do LibRaw, Ghostscript. |
| R7. Pacote demasiado grande (150–250 MB instalado) | Alta | Baixo | Podar módulos Qt e do imagecodecs. Sem UPX, porque agrava os falsos positivos. |
| R8. O GIL bloqueia a interface durante descodificações | Baixa–Média | Médio | Spike S4. Recorrer a um pool de processos para os descodificadores que não libertem o GIL. |
| R9. Regressões de Qt/PySide6 em Wayland ou com escala fracionária | Média | Médio | Fixar versões e manter a matriz de testes. |
| R10. O âmbito cresce (edição, IA) | Alta | Médio | Contrato de âmbito da §6.1; os opcionais entram só depois da v1. |
| R11. Bibliotecas de formatos mantidas por equipas muito pequenas `[conhecimento]` | Média | Médio | O registo de codecs isola cada uma, e é possível substituí-las ou recompilá-las. |

### 7.2 Decisões pendentes

Nenhuma destas decisões altera a arquitetura. Em cada uma está a proposta que adotei por omissão.

- **D1. Arquiteturas suportadas.** Proposta: macOS arm64 e x64 enquanto existirem wheels; Windows x64; Windows arm64 na v1.1; Linux x64.
- **D2. Versões mínimas dos sistemas.** Proposta: Windows 11, com Windows 10 22H2 em "melhor esforço" (o suporte da Microsoft já terminou `[conhecimento]`); a versão mínima de macOS do Qt 6.11, que está por confirmar; glibc ≥ 2.28 (a wheel do pi-heif é manylinux_2_28 `[teste]`).
- **D3. HEIC.** Proposta: incluir pi-heif apenas com parecer jurídico favorável; caso contrário, usar os codecs do sistema.
- **D4. Instância única.** Proposta: sim por omissão, reutilizando a janela aberta.
- **D5. Ampliação.** Proposta: vizinho mais próximo a partir de 200 %.
- **D6. Intenção de renderização por omissão.** Proposta: colorimétrica relativa com BPC.
- **D7. Identidade e certificados.** Nome, ícone, bundle id (`com.cristallumnis.imageviewer`), Apple Developer ID e certificado Authenticode (por exemplo, Azure Trusted Signing).
- **D8. Atualizações automáticas.** Proposta: nenhuma na v1.
- **D9. Telemetria e relatórios de falhas.** Proposta: nenhuma por omissão.
- **D10. OpenImageIO e EXR.** Proposta: na v1.1.

### 7.3 O que não foi possível confirmar

- **Windows e macOS:** nada foi testado nestes sistemas. Ficam por medir o arranque, o comportamento dos antivírus, a notarização e a cadeia de apresentação.
- **macOS:** se o Qt marca a superfície da janela com o espaço do ecrã ou com sRGB.
- **Windows 11:** a interação com o ACM.
- **Wayland:** se o PySide6 expõe a marcação de espaço de cor da superfície. O Qt 6.11 inclui o protocolo, mas não confirmei que seja utilizável a partir de Python.
- **pi-heif:** que as wheels de Windows e macOS também excluem o x265. Só verifiquei a wheel Linux.
- **Disponibilidade por plataforma:** a versão mínima de macOS do PySide6 6.11 e se existem wheels win-arm64 para todas as dependências.
- **Magick.NET:** não testei em Windows nem em macOS via pythonnet, nem a notarização com CoreCLR. O modelo de memória HDRI é conhecimento de documentação, não foi medido.
- **Briefcase:** não o testei de ponta a ponta.
- **GIL:** não verifiquei se cada descodificador liberta o GIL.
- **Benchmarks:** usaram imagens sintéticas de ruído, num contentor com 4 vCPU. Fotografias reais darão tempos diferentes.

### 7.4 Go / No-go

**GO para a Fase 0.** `[confiança média-alta]`, pelas seguintes razões:
- arranque congelado de 0,21–0,28 s medido em Linux;
- wheels nativas congeladas sem hooks;
- pipeline de 16 bits e LittleCMS validados;
- Magick.NET descartado com base em medição.

A incerteza que resta concentra-se no Windows (arranque e antivírus) e na cadeia de apresentação do macOS e do Wayland.

**GO condicional para as Fases 1 e seguintes.** Depende de os critérios (a) a (e) da Fase 0 serem cumpridos. Se (a) ou (c) falharem depois de mitigação, a decisão passa a C++/Qt, mantendo a mesma arquitetura. Se o spike S2 mostrar que um sistema não permite controlar a conversão de cor, continuamos, mas a promessa de fidelidade nesse sistema é reduzida e declarada.
