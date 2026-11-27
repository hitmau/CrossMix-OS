# CrossMix Desktop

Desktop leve de desenvolvimento para o TrimUI Smart Pro, feito em SDL2 e SDL_ttf. O layout se inspira no Zorin OS — área de trabalho limpa, painel inferior, dock e menu Iniciar pesquisável — com identidade própria e sem copiar imagens ou logotipos do Zorin. O aplicativo abre em tela cheia e não substitui o CrossMix nem altera o boot.

## O que funciona

- Menu Iniciar com filtro por nome e atalhos para Internet, editor, Commander, Terminal, Python e retorno ao CrossMix.
- Ponteiro visível controlado pelo analógico direito (eixos SDL 2/3); A confirma no ponto do ponteiro. O direcional e o analógico esquerdo navegam pelos itens. Mouse e teclado SDL também são aceitos.
- Editor integrado com lista real de arquivos `.py`, `.js`, `.txt` e `.md` do workspace, criação, edição, salvamento e execução de Python. X executa, Y salva e SELECT abre o teclado virtual.
- Navegador leve dentro da janela: pesquisa HTTPS pelo Bing, abre resultados e mostra o texto e links de páginas HTTP/HTTPS. Usa `urllib` e `html.parser` da biblioteca padrão do Python do CrossMix; mantém validação TLS e não precisa de X11/Wayland.
- Painel inferior lê bateria, memória e espaço livre do cartão pelos dados do Linux. Valores indisponíveis são omitidos.
- Terminal e Dingux Commander abrem os aplicativos existentes do CrossMix como processos filhos e devolvem o foco ao Desktop ao fechar.

## Limites conhecidos

O navegador é um leitor de texto com navegação por links; não executa JavaScript e não reproduz o layout, imagens ou mídia das páginas. NetSurf foi avaliado como alternativa upstream, mas o frontend framebuffer exige um port e dependências que precisam ser compilados para a ABI do CrossMix. O editor atual preserva compatibilidade SDL2; Lite XL ainda exige build próprio para o console. Python 3.11 é usado pelo CrossMix; Node.js não vem neste pacote porque a ABI do aparelho precisa ser confirmada. Arquivos `.js` podem ser editados e só executam quando existe um `System/bin/node` compatível ou `node` no `PATH`.

O eixo SDL 2/3 é o mapeamento padrão para o analógico direito do controle TrimUI; a identificação física, mouse/teclado USB ou Bluetooth e leitura dos indicadores precisam ser confirmadas no aparelho. Esta versão continua sendo um shell de uma janela SDL — não há gerenciador Linux multi-janelas no CrossMix usado aqui.

## Compilar para o TrimUI

Na raiz do checkout, rode `bash Apps/Desktop/desktop/build-arm64.sh` em um ambiente com Docker e a imagem `trimui-dev-gui:tg5040`. O executável ARM64 fica em `Apps/Desktop/bin/desktop`.

## Instalar no cartão

O pacote ZIP mantém a pasta `Apps/Desktop`. Extraia em qualquer local do cartão e execute `Apps/Desktop/install-package.sh` pelo Commander. O script copia os arquivos para `/mnt/SDCARD/Apps/Desktop`; não altera boot, outras aplicações ou o Workspace.

## Prévia no PC

No Ubuntu/WSLg, instale `build-essential`, `pkg-config`, `libsdl2-dev` e `libsdl2-ttf-dev`; execute `Apps/Desktop/desktop/run-desktop.ps1`. A prévia usa uma pasta de dados separada em `Apps/Desktop/desktop/.runtime`.

Para capturar uma tela na prévia SDL, defina `CROSSMIX_DESKTOP_CAPTURE` com o caminho de um arquivo BMP. `CROSSMIX_DESKTOP_START_MENU=1` abre a captura com o menu visível; `CROSSMIX_DESKTOP_START_BROWSER_QUERY="CrossMix OS"` inicia uma pesquisa real antes da captura.

## Arquivos do usuário

O aplicativo mantém projetos em `/mnt/SDCARD/Data/Desktop/Workspace`. Na primeira abertura, os exemplos `hello.py` e `hello.js` são copiados para esse workspace sem sobrescrever arquivos existentes.
