# CrossMix Desktop

Aplicativo independente para abrir uma area de trabalho de desenvolvimento no TrimUI Smart Pro. A interface e desenhada em SDL2 e SDL2_ttf, com inspiracao no painel, menu de aplicativos e dock do Zorin OS. Nao substitui o CrossMix nem altera o boot.

## Primeira versao

- Home com menu Iniciar, atalhos e barra inferior.
- Navegacao por controle, mouse e teclado no mesmo loop SDL.
- Editor simples integrado, com abertura dos exemplos Python e JavaScript e salvamento no workspace.
- Execucao de Python em processo separado, com saida capturada no painel do editor.
- Terminal e Dingux Commander abertos como apps de tela cheia; ao fechar, o Desktop volta.
- Node.js aparece como indisponivel quando nao existe um binario compativel no CrossMix.

### Controles

- Direcional: navegar no menu ou no texto; analógico esquerdo: mover o ponteiro.
- A: selecionar/teclar; B: voltar; START: abrir o menu; SELECT: teclado virtual no editor.
- X: executar o arquivo atual; Y: salvar. Teclado e mouse SDL podem ser usados junto com os controles.
- A leitura de teclado, mouse e joystick ocorre no mesmo loop SDL; o reconhecimento USB OTG/Bluetooth ainda precisa de confirmacao no aparelho.

Esta versao ainda nao e um gerenciador Linux multi-janela. O CrossMix distribuido aqui nao inclui um servidor X11/Wayland ou um window manager identificado; por isso o terminal e o gerenciador de arquivos sao abertos como aplicativos filhos. A escolha de uma pilha multi-janela depende da verificacao no aparelho.

O editor desta primeira versao e integrado e compacto. Lite XL e uma alternativa a avaliar para uma experiencia de codigo mais completa; ele usa SDL2, Lua 5.4, PCRE2 e FreeType, entao precisa ser compilado e medido para a ABI do console antes de ser empacotado. Node.js tambem fica pendente da medicao: as builds Linux ARM64 oficiais atuais exigem kernel 5.14 e glibc 2.34 ou superior ([matriz oficial](https://github.com/nodejs/node/blob/main/BUILDING.md)); esses valores ainda nao foram lidos no TrimUI.

## Compilar para o console

Na raiz do checkout, rode `bash Apps/Desktop/desktop/build-arm64.sh` em um ambiente com Docker e a imagem `trimui-dev-gui:tg5040`. O executavel gerado fica em `Apps/Desktop/bin/desktop`.

## Previa no PC

No Ubuntu/WSLg, instale `build-essential`, `pkg-config`, `libsdl2-dev` e `libsdl2-ttf-dev`; depois execute `Apps/Desktop/desktop/run-desktop.ps1` no PowerShell. A previa usa uma pasta de dados separada em `Apps/Desktop/desktop/.runtime`.

## Dados

O app guarda projetos e arquivos em `/mnt/SDCARD/Data/Desktop/Workspace`. Os exemplos iniciais estao em `Data/Desktop/Workspace`.
