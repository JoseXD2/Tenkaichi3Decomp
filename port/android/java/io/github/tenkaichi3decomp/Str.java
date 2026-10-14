/*
 * The launcher's words in English, Portuguese and Spanish: the phone's language unless another is picked in the
 * launcher (Settings > Game > Language). Anything else falls back to English.
 */
package io.github.tenkaichi3decomp;

import java.util.HashMap;
import java.util.Locale;

final class Str {
    static int sLang; // 0 English, 1 Portuguese, 2 Spanish
    private static final HashMap<String, String[]> M = new HashMap<>();

    private Str() {}

    /* "auto" (the phone's language), "en", "pt" or "es" */
    static void use(String choice) {
        String l = choice == null || choice.equals("auto") ? Locale.getDefault().getLanguage() : choice;
        sLang = l.equals("pt") ? 1 : l.equals("es") ? 2 : 0;
    }

    static String t(String key) {
        String[] v = M.get(key);
        if (v == null) { return key; }
        return v[sLang] != null ? v[sLang] : v[0];
    }

    static String f(String key, Object... args) { return String.format(Locale.ROOT, t(key), args); }

    private static void a(String k, String en, String pt, String es) { M.put(k, new String[] {en, pt, es}); }

    static {
        // ---- the side bar and home
        a("play", "Play", "Jogar", "Jugar");
        a("continue", "Continue", "Continuar", "Continuar");
        a("home", "Home", "Início", "Inicio");
        a("settings", "Settings", "Configurações", "Ajustes");
        a("mods", "Mods", "Mods", "Mods");
        a("about", "About", "Sobre", "Acerca de");
        a("ready", "Ready to play", "Pronto para jogar", "Listo para jugar");
        a("ready_sub", "The game is installed.", "O jogo está instalado.", "El juego está instalado.");
        a("st_data", "Game data", "Dados do jogo", "Datos del juego");
        a("st_data_ok", "installed from your disc", "instalados do seu disco", "instalados desde tu disco");
        a("st_gpu", "Graphics chip", "Chip gráfico", "Chip gráfico");
        a("st_gpu_wait", "checking...", "verificando...", "comprobando...");
        a("st_profile", "Recommended for it", "Recomendado para ele", "Recomendado para él");
        a("st_folder", "Data folder", "Pasta de dados", "Carpeta de datos");
        a("st_mods", "Mods on", "Mods ligados", "Mods activos");
        a("st_none", "none", "nenhum", "ninguno");
        a("hint_ingame", "In game, Android's BACK gesture, the controller's PS button or L3 + R3 open the game's own menu "
                + "(controls layout, online, cheats and more).",
                "No jogo, o gesto VOLTAR do Android, o botão PS do controle ou L3 + R3 abrem o menu do próprio jogo "
                + "(posição dos controles, online, trapaças e mais).",
                "En el juego, el gesto ATRÁS de Android, el botón PS del mando o L3 + R3 abren el menú del propio juego "
                + "(posición de los controles, en línea, trucos y más).");
        a("restart_note", "Changes apply when the game starts. A game left open is restarted.",
                "As mudanças valem quando o jogo inicia. Um jogo deixado aberto é reiniciado.",
                "Los cambios se aplican al iniciar el juego. Un juego dejado abierto se reinicia.");
        a("reinstall", "Reinstall from ISO", "Reinstalar pela ISO", "Reinstalar desde ISO");

        // ---- settings tabs
        a("tab_gfx", "Graphics", "Gráficos", "Gráficos");
        a("tab_perf", "Performance", "Desempenho", "Rendimiento");
        a("tab_ctl", "Controls", "Controles", "Controles");
        a("tab_audio", "Audio", "Áudio", "Audio");
        a("tab_game", "Game", "Jogo", "Juego");

        a("res", "Internal resolution", "Resolução interna", "Resolución interna");
        a("res_tip", "How finely the game is drawn. 1x is the PlayStation 2's own. Higher is sharper and heavier.",
                "Quão nítido o jogo é desenhado. 1x é a do PlayStation 2. Mais alto é mais nítido e mais pesado.",
                "Qué tan nítido se dibuja el juego. 1x es la del PlayStation 2. Más alto es más nítido y más pesado.");
        a("aspect", "Aspect ratio", "Proporção da tela", "Relación de aspecto");
        a("aspect_tip", "Wider than 4:3 shows more of the fight to the sides.",
                "Mais largo que 4:3 mostra mais da luta nos lados.", "Más ancho que 4:3 muestra más de la pelea a los lados.");
        a("whole", "Whole screen", "Tela inteira", "Pantalla completa");
        a("f2d", "2D filtering", "Filtro 2D", "Filtro 2D");
        a("f2d_tip", "How menus and text are drawn above 1x.", "Como menus e textos são desenhados acima de 1x.",
                "Cómo se dibujan menús y textos por encima de 1x.");
        a("sharp", "Sharp (console)", "Nítido (console)", "Nítido (consola)");
        a("smooth", "Smooth", "Suave", "Suave");
        a("texpack", "Texture pack", "Pacote de texturas", "Paquete de texturas");
        a("texpack_tip", "Replacement textures from the folder %s.", "Texturas substitutas da pasta %s.",
                "Texturas de reemplazo de la carpeta %s.");
        a("hud", "HUD style", "Estilo do HUD", "Estilo del HUD");
        a("hud0", "Screen edges", "Bordas da tela", "Bordes");
        a("hud1", "Long health bars", "Barras longas", "Barras largas");
        a("hud2", "Middle (4:3)", "No meio (4:3)", "Centro (4:3)");
        a("menus43", "Menus in 4:3", "Menus em 4:3", "Menús en 4:3");
        a("menus43_tip", "Menus at their own shape with bars at the sides; fights stay wide.",
                "Menus no formato original com barras nos lados; as lutas continuam largas.",
                "Menús en su forma original con barras a los lados; las peleas siguen anchas.");
        a("effects", "Effects", "Efeitos", "Efectos");
        a("fx0", "Outline", "Contorno", "Contorno");
        a("fx1", "See-through tint", "Tom de transparência", "Tinte translúcido");
        a("fx2", "Depth tint", "Névoa de distância", "Niebla de distancia");
        a("fx3", "Glare and glow", "Brilho e reflexo", "Brillo y resplandor");
        a("fx4", "Distance blur", "Desfoque de distância", "Desenfoque de distancia");
        a("fx5", "Movement blur", "Desfoque de movimento", "Desenfoque de movimiento");
        a("glow", "Glow strength", "Intensidade do brilho", "Intensidad del brillo");

        a("preset", "Profile", "Perfil", "Perfil");
        a("preset_tip", "Sets resolution and frame rate at once. Recommended for your chip: %s.",
                "Ajusta resolução e quadros de uma vez. Recomendado para o seu chip: %s.",
                "Ajusta resolución y cuadros a la vez. Recomendado para tu chip: %s.");
        a("p_battery", "Battery saver", "Economia", "Ahorro");
        a("p_balanced", "Balanced", "Equilibrado", "Equilibrado");
        a("p_quality", "Quality", "Qualidade", "Calidad");
        a("p_custom", "Custom", "Personalizado", "Personalizado");
        a("fps", "Smooth motion", "Movimento suave", "Movimiento suave");
        a("fps_tip", "The fight is computed 30 times a second; more pictures are drawn in between. Heavier on the chip.",
                "A luta é calculada 30 vezes por segundo; mais quadros são desenhados entre eles. Pesa mais no chip.",
                "La pelea se calcula 30 veces por segundo; se dibujan más cuadros entre ellos. Más pesado para el chip.");
        a("fps0", "30 (console)", "30 (console)", "30 (consola)");
        a("skip", "Skip pictures instead of slowing down", "Pular quadros em vez de ficar lento", "Saltar cuadros en vez de ir lento");
        a("skip_tip", "When the phone cannot keep up, the game stays at full speed and leaves pictures out.",
                "Quando o celular não acompanha, o jogo mantém a velocidade e pula quadros.",
                "Cuando el teléfono no da abasto, el juego mantiene la velocidad y salta cuadros.");
        a("vsync", "V-sync", "V-sync", "V-sync");
        a("vsync_tip", "Waits for the screen's refresh. Turn on if the picture tears; adds a little delay.",
                "Espera a atualização da tela. Ligue se a imagem rasgar; adiciona um pouco de atraso.",
                "Espera el refresco de la pantalla. Actívalo si la imagen se corta; añade algo de retraso.");
        a("meter", "Show frame rate", "Mostrar FPS", "Mostrar FPS");
        a("fastz", "Fast depth", "Profundidade rápida", "Profundidad rápida");
        a("fastz_tip", "The chip skips hidden pixels before drawing them: faster at 3x and up. Surfaces drawn on top of each other "
                + "may flicker a little where they meet.",
                "O chip pula pixels escondidos antes de desenhá-los: mais rápido em 3x ou mais. Superfícies desenhadas uma sobre a outra "
                + "podem piscar um pouco onde se encontram.",
                "El chip omite píxeles ocultos antes de dibujarlos: más rápido en 3x o más. Superficies dibujadas una sobre otra "
                + "pueden parpadear un poco donde se juntan.");
        a("sustained", "Sustained performance", "Desempenho sustentado", "Rendimiento sostenido");
        a("sustained_tip", "Asks Android for a steady speed that does not drop when the phone heats up (where supported).",
                "Pede ao Android uma velocidade estável que não cai quando o celular esquenta (onde houver suporte).",
                "Pide a Android una velocidad estable que no baja cuando el teléfono se calienta (si se admite).");
        a("safe", "Compatibility mode", "Modo de compatibilidade", "Modo de compatibilidad");
        a("safe_tip", "For chips whose drivers misbehave (black screen, crashes, flicker): the plainest graphics path.",
                "Para chips com drivers problemáticos (tela preta, travamentos, piscadas): o caminho gráfico mais simples.",
                "Para chips con drivers problemáticos (pantalla negra, cierres, parpadeo): la ruta gráfica más simple.");

        a("touch", "Touch controls", "Controles na tela", "Controles táctiles");
        a("touch0", "Off", "Desligado", "Apagado");
        a("touch1", "Auto (hidden with a controller)", "Auto (some com controle)", "Auto (ocultos con mando)");
        a("touch2", "Always", "Sempre", "Siempre");
        a("touch_size", "Size", "Tamanho", "Tamaño");
        a("touch_alpha", "Opacity", "Opacidade", "Opacidad");
        a("touch_edit", "To move or resize single buttons: in game, open the menu > Controls > Edit layout.",
                "Para mover ou redimensionar botões: no jogo, abra o menu > Controles > Editar posição.",
                "Para mover o cambiar el tamaño de botones: en el juego, abre el menú > Controles > Editar.");

        a("music", "Music", "Música", "Música");
        a("sfx", "Sound effects", "Efeitos sonoros", "Efectos de sonido");
        a("voices", "Voices", "Vozes", "Voces");
        a("stage", "Stage sounds", "Sons do cenário", "Sonidos del escenario");
        a("follows", "Music follows game speed", "Música segue a velocidade do jogo", "Música sigue la velocidad del juego");

        a("skip_logos", "Skip the company logos", "Pular logos das empresas", "Saltar logos de empresas");
        a("skip_intro", "Skip the intro movie", "Pular vídeo de abertura", "Saltar video de introducción");
        a("skip_copy", "Skip the copyright screen", "Pular tela de copyright", "Saltar pantalla de copyright");
        a("discord", "Discord status", "Status no Discord", "Estado en Discord");
        a("discord_tip", "Shows what you are playing on your Discord profile (needs the Discord app).",
                "Mostra o que você está jogando no seu perfil do Discord (precisa do app do Discord).",
                "Muestra a qué juegas en tu perfil de Discord (necesita la app de Discord).");
        a("lang", "Language", "Idioma", "Idioma");
        a("lang_auto", "Phone's", "Do celular", "Del teléfono");

        // ---- mods
        a("mods_sub", "Each mod is a folder in %s with files laid out as in the game data (pzs3us1/01234.bin, disc/...). "
                + "Switched-on mods are used from the top: the first that has a file wins.",
                "Cada mod é uma pasta em %s com os arquivos no formato dos dados do jogo (pzs3us1/01234.bin, disc/...). "
                + "Os mods ligados valem de cima para baixo: o primeiro que tiver um arquivo ganha.",
                "Cada mod es una carpeta en %s con los archivos como en los datos del juego (pzs3us1/01234.bin, disc/...). "
                + "Los mods activos se usan de arriba abajo: gana el primero que tenga un archivo.");
        a("mods_import", "Import .zip", "Importar .zip", "Importar .zip");
        a("mods_refresh", "Refresh", "Atualizar", "Actualizar");
        a("mods_empty", "No mods yet. Import a .zip or copy a mod's folder into the mods folder.",
                "Nenhum mod ainda. Importe um .zip ou copie a pasta de um mod para a pasta mods.",
                "Aún no hay mods. Importa un .zip o copia la carpeta de un mod en la carpeta mods.");
        a("mods_files", "%d files, %s", "%d arquivos, %s", "%d archivos, %s");
        a("mods_importing", "Importing %s...", "Importando %s...", "Importando %s...");
        a("mods_bad", "This .zip has no game files (pzs3us0/1/2 or disc folders).",
                "Esse .zip não tem arquivos do jogo (pastas pzs3us0/1/2 ou disc).",
                "Este .zip no tiene archivos del juego (carpetas pzs3us0/1/2 o disc).");
        a("mods_delete", "Delete the mod %s?", "Apagar o mod %s?", "¿Borrar el mod %s?");
        a("mods_online", "Online, both players need the same mods on.", "No online, os dois jogadores precisam dos mesmos mods ligados.",
                "En línea, ambos jugadores necesitan los mismos mods activos.");
        a("yes", "Delete", "Apagar", "Borrar");
        a("no", "Cancel", "Cancelar", "Cancelar");

        // ---- about
        a("about_sub", "Budokai Tenkaichi 3 decompilation, ported to Android.", "Decompilação de Budokai Tenkaichi 3, portada para Android.",
                "Decompilación de Budokai Tenkaichi 3, portada a Android.");
        a("about_legal", "Dragon Rage runs your own copy of the game: its data comes from your own PlayStation 2 disc. "
                + "Share only the app (APK), never the ISO or the game data.",
                "O Dragon Rage roda a sua própria cópia do jogo: os dados vêm do seu próprio disco de PlayStation 2. "
                + "Compartilhe só o app (APK), nunca a ISO nem os dados do jogo.",
                "Dragon Rage ejecuta tu propia copia del juego: los datos vienen de tu propio disco de PlayStation 2. "
                + "Comparte solo la app (APK), nunca la ISO ni los datos del juego.");
        a("version", "Version", "Versão", "Versión");
        a("device", "Device", "Aparelho", "Dispositivo");
        a("use_root", "Use the DragonRage folder at the root of the storage", "Usar a pasta DragonRage na raiz do armazenamento",
                "Usar la carpeta DragonRage en la raíz del almacenamiento");

        // ---- setup pages
        a("folder_title", "Data folder", "Pasta de dados", "Carpeta de datos");
        a("folder_sub", "Where the game keeps your textures, mods, saves and settings.", "Onde o jogo guarda suas texturas, mods, saves e configurações.",
                "Donde el juego guarda tus texturas, mods, partidas y ajustes.");
        a("folder_text", "To use the \"%s\" folder at the root of the storage (easy to find in a file manager, to drop texture "
                + "packs and mods in), Android asks for \"All files access\". On the screen that opens, turn it on for Dragon Rage and come back.",
                "Para usar a pasta \"%s\" na raiz do armazenamento (fácil de achar no gerenciador de arquivos, para colocar texturas "
                + "e mods), o Android pede \"Acesso a todos os arquivos\". Na tela que abrir, ative para o Dragon Rage e volte.",
                "Para usar la carpeta \"%s\" en la raíz del almacenamiento (fácil de encontrar en un gestor de archivos, para poner "
                + "texturas y mods), Android pide \"Acceso a todos los archivos\". En la pantalla que se abre, actívalo para Dragon Rage y vuelve.");
        a("not_now", "Not now", "Agora não", "Ahora no");
        a("allow", "Allow", "Permitir", "Permitir");
        a("moving", "Moving the data", "Movendo os dados", "Moviendo los datos");
        a("moving_sub", "Your files are moving to the %s folder at the root of the storage. Only this once.",
                "Seus arquivos estão indo para a pasta %s na raiz do armazenamento. Só desta vez.",
                "Tus archivos se mueven a la carpeta %s en la raíz del almacenamiento. Solo esta vez.");
        a("moving_fail", "Could not move everything.", "Não foi possível mover tudo.", "No se pudo mover todo.");
        a("again", "Try again", "Tentar de novo", "Reintentar");
        a("install_sub", "This installs the game on your phone from your own PlayStation 2 disc.",
                "Isto instala o jogo no seu celular a partir do seu próprio disco de PlayStation 2.",
                "Esto instala el juego en tu teléfono desde tu propio disco de PlayStation 2.");
        a("install_text", "Pick the image of your Dragon Ball Z: Budokai Tenkaichi 3 disc, US version (SLUS-21678), as an .iso file. "
                + "The game data is taken from it and stays on this phone. Nothing is downloaded.",
                "Escolha a imagem do seu disco de Dragon Ball Z: Budokai Tenkaichi 3, versão americana (SLUS-21678), em arquivo .iso. "
                + "Os dados do jogo são tirados dela e ficam neste celular. Nada é baixado.",
                "Elige la imagen de tu disco de Dragon Ball Z: Budokai Tenkaichi 3, versión estadounidense (SLUS-21678), como archivo .iso. "
                + "Los datos del juego se sacan de ella y quedan en este teléfono. No se descarga nada.");
        a("choose_iso", "Choose ISO...", "Escolher ISO...", "Elegir ISO...");
        a("close", "Close", "Fechar", "Cerrar");
        a("back", "Back", "Voltar", "Volver");
        a("installing", "Installing", "Instalando", "Instalando");
        a("installing_sub", "The game data is being taken from your disc image. This takes a few minutes.",
                "Os dados do jogo estão sendo tirados da imagem do seu disco. Leva alguns minutos.",
                "Los datos del juego se están sacando de la imagen de tu disco. Tarda unos minutos.");
        a("reading", "Reading the disc image", "Lendo a imagem do disco", "Leyendo la imagen del disco");
    }
}
