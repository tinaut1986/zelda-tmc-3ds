#include "port_text_i18n.h"

#include <string.h>

/* One row per English UI string, columns in PortTextLanguage order:
 * EN, FR, DE, ES, IT. The source is UTF-8; the renderer maps it to the
 * game's charmap (see Port_Text_Utf8Next). Text is kept short: the
 * bottom-screen font is wide and chips are sized to their words. */
static const char* const kEntries[][PORT_TEXT_COUNT] = {
    { "QUEST", "QUÊTE", "QUEST", "MISIÓN", "MISSIONE" },
    { "MAP", "CARTE", "KARTE", "MAPA", "MAPPA" },
    { "ITEMS", "OBJETS", "ITEMS", "OBJETOS", "OGGETTI" },
    { "BACK", "RETOUR", "ZURÜCK", "ATRÁS", "INDIETRO" },
    { "CANCEL", "ANNULER", "ABBRECHEN", "CANCELAR", "ANNULLA" },
    { "LOAD", "CHARGER", "LADEN", "CARGAR", "CARICA" },
    { "DONE", "FAIT", "FERTIG", "HECHO", "FATTO" },
    { "WRITE", "ÉCRIRE", "SCHREIBEN", "ESCRIBIR", "SCRIVI" },
    { "TOP HUD", "HUD HAUT", "HUD OBEN", "HUD SUPERIOR", "HUD ALTO" },
    { "WIDESCREEN", "PANORAMIQUE", "BREITBILD", "PANTALLA ANCHA", "SCHERMO LARGO" },
    { "FOLLOW CAM", "CAMÉRA SUIVI", "KAMERA FOLGT", "CÁMARA SIGUE", "CAMERA SEGUE" },
    { "WINDCREST PINS", "REPÈRES VENT", "WINDMARKEN", "MARCAS DE VIENTO", "SEGNALI VENTO" },
    { "FLOOR AUTO RETURN", "RETOUR ÉTAGE AUTO", "ETAGE AUTO ZURÜCK", "VOLVER PISO AUTO", "PIANO AUTO INDIETRO" },
    { "TURBO SPEED", "VITESSE TURBO", "TURBO-TEMPO", "VELOCIDAD TURBO", "VELOCITÀ TURBO" },
    { "MASTER VOLUME", "VOLUME GÉNÉRAL", "LAUTSTÄRKE", "VOLUMEN GENERAL", "VOLUME GENERALE" },
    { "AUTOSAVE", "SAUVEGARDE AUTO", "AUTOSPEICHERN", "GUARDADO AUTO", "SALVA AUTO" },
    { "COLOR CORRECTION", "CORRECTION COULEUR", "FARBKORREKTUR", "CORRECCIÓN COLOR", "CORREZ. COLORE" },
    { "SHOW FPS", "AFFICHER FPS", "FPS ANZEIGEN", "MOSTRAR FPS", "MOSTRA FPS" },
    { "HOLD TO ADVANCE TEXT", "MAINTENIR: TEXTE SUIVANT", "HALTEN: TEXT WEITER", "MANTENER: AVANZAR TEXTO", "TIENI: AVANZA TESTO" },
    { "RANDOMIZER", "RANDOMISEUR", "RANDOMIZER", "RANDOMIZADOR", "RANDOMIZER" },
    { "PANEL BACKDROP", "FOND DU PANNEAU", "PANEL-HINTERGRUND", "FONDO DEL PANEL", "SFONDO PANNELLO" },
    { "SWAP SCREENS", "ÉCHANGER ÉCRANS", "SCREENS TAUSCHEN", "INTERCAMBIAR PANTALLAS", "SCAMBIA SCHERMI" },
    { "ASPECT RATIO", "FORMAT IMAGE", "SEITENVERHÄLTNIS", "RELACIÓN ASPECTO", "FORMATO IMMAGINE" },
    { "DISPLAY STYLE", "STYLE AFFICHAGE", "ANZEIGESTIL", "ESTILO IMAGEN", "STILE VISUALE" },
    { "FRAMES", "IMAGES", "FRAMES", "FOTOGRAMAS", "FRAME" },
    { "SHOW", "VOIR", "ZEIGEN", "VER", "MOSTRA" },
    { "HIDE", "CACHER", "VERBERGEN", "OCULTAR", "NASCONDI" },
    { "ON", "OUI", "AN", "SÍ", "SÌ" },
    { "OFF", "NON", "AUS", "NO", "NO" },
    { "RESTART", "REDÉMARRER", "NEUSTART", "REINICIAR", "RIAVVIA" },
    { "ORIGINAL", "ORIGINAL", "ORIGINAL", "ORIGINAL", "ORIGINALE" },
    { "PIXEL PERFECT", "PIXEL PARFAIT", "PIXELGENAU", "PÍXEL EXACTO", "PIXEL PERFETTO" },
    { "BLUR", "FLOU", "WEICH", "SUAVE", "SFUOCATO" },
    { "PATTERN", "MOTIF", "MUSTER", "PATRÓN", "MOTIVO" },
    { "CREAM", "CRÈME", "CREME", "CREMA", "CREMA" },
    { "DARK", "SOMBRE", "DUNKEL", "OSCURO", "SCURO" },
    { "DIM", "TAMISÉ", "GEDIMMT", "TENUE", "SOFFUSO" },
    { "STONE", "PIERRE", "STEIN", "PIEDRA", "PIETRA" },
    { "SLATE", "ARDOISE", "SCHIEFER", "PIZARRA", "ARDESIA" },
    { "NAVY", "MARINE", "MARINE", "MARINO", "BLU" },
    { "LOCK 30", "FIXE 30", "FIX 30", "FIJO 30", "FISSO 30" },
    { "NO SKIP", "SANS SAUT", "KEIN SKIP", "SIN SALTOS", "NO SALTI" },
    { "SCREEN", "ÉCRAN", "BILDSCHIRM", "PANTALLA", "SCHERMO" },
    { "GAMEPLAY", "JEU", "SPIEL", "JUEGO", "GIOCO" },
    { "DEVELOPER", "DÉVELOPPEUR", "ENTWICKLER", "DESARROLLO", "SVILUPPO" },
    { "OVERLAY", "SUPERPOSITION", "OVERLAY", "SUPERPOSICIÓN", "OVERLAY" },
    { "UPDATE", "MISE À JOUR", "UPDATE", "ACTUALIZAR", "AGGIORNA" },
    { "SETTINGS", "RÉGLAGES", "OPTIONEN", "AJUSTES", "OPZIONI" },
    { "MEM DUMP", "COPIE MÉMOIRE", "SPEICHERABBILD", "VOLCADO MEMORIA", "DUMP MEMORIA" },
    { "LOAD STATE", "CHARGER ÉTAT", "STATUS LADEN", "CARGAR ESTADO", "CARICA STATO" },
    { "VERSION", "VERSION", "VERSION", "VERSIÓN", "VERSIONE" },
    { "MODEL", "MODÈLE", "MODELL", "MODELO", "MODELLO" },
    { "FPS NOW", "FPS ACTUEL", "FPS JETZT", "FPS AHORA", "FPS ORA" },
    { "FPS AVG", "FPS MOYEN", "FPS MITTEL", "FPS MEDIO", "FPS MEDI" },
    { "AREA", "ZONE", "BEREICH", "ZONA", "AREA" },
    { "ROOM", "SALLE", "RAUM", "SALA", "STANZA" },
    { "WIDE", "LARGE", "BREIT", "ANCHO", "LARGO" },
    { "NATIVE", "NATIF", "NATIV", "NATIVO", "NATIVO" },
    { "LOAD LATEST DUMP?", "CHARGER LA DERNIÈRE COPIE ?", "NEUESTEN DUMP LADEN?", "¿CARGAR ÚLTIMO VOLCADO?", "CARICARE L'ULTIMO DUMP?" },
    { "THE LATEST DUMP IN THE", "LA DERNIÈRE COPIE DU", "DER NEUESTE DUMP IM", "EL ÚLTIMO VOLCADO DE LA", "L'ULTIMO DUMP NELLA" },
    { "DUMPS FOLDER WILL REPLACE", "DOSSIER DUMPS REMPLACERA", "DUMPS-ORDNER ERSETZT", "CARPETA DUMPS REEMPLAZARÁ", "CARTELLA DUMPS SOSTITUIRÀ" },
    { "THE CURRENT GAME STATE.", "L'ÉTAT ACTUEL DU JEU.", "DEN AKTUELLEN SPIELSTAND.", "EL ESTADO ACTUAL DEL JUEGO.", "LO STATO ATTUALE DEL GIOCO." },
    { "UNSAVED PROGRESS MAY BE LOST.", "PROGRESSION NON SAUVÉE PERDUE.", "UNGESPEICHERTES GEHT VERLOREN.", "SE PIERDE LO NO GUARDADO.", "PROGRESSI NON SALVATI PERSI." },
    { "THE GAME WILL RESTART.", "LE JEU VA REDÉMARRER.", "DAS SPIEL STARTET NEU.", "EL JUEGO SE REINICIARÁ.", "IL GIOCO VERRÀ RIAVVIATO." },
    { "THIS OPTION IS NOT", "CETTE OPTION N'EST PAS", "DIESE OPTION IST", "ESTA OPCIÓN AÚN NO", "QUESTA OPZIONE NON È" },
    { "AVAILABLE YET.", "ENCORE DISPONIBLE.", "NOCH NICHT VERFÜGBAR.", "ESTÁ DISPONIBLE.", "ANCORA DISPONIBILE." },
    { "DROP", "POSER", "ABLEGEN", "SOLTAR", "LASCIA" },
    { "THROW", "LANCER", "WERFEN", "LANZAR", "LANCIA" },
    { "READ", "LIRE", "LESEN", "LEER", "LEGGI" },
    { "CHECK", "EXAMINER", "ANSEHEN", "MIRAR", "ESAMINA" },
    { "OPEN", "OUVRIR", "ÖFFNEN", "ABRIR", "APRI" },
    { "SPEAK", "PARLER", "SPRECHEN", "HABLAR", "PARLA" },
    { "GRAB", "PRENDRE", "GREIFEN", "AGARRAR", "AFFERRA" },
    { "LIFT", "SOULEVER", "HEBEN", "LEVANTAR", "SOLLEVA" },
    { "GROW", "GRANDIR", "WACHSEN", "CRECER", "INGRANDISCI" },
    { "SHRINK", "RÉTRÉCIR", "SCHRUMPFEN", "ENCOGER", "RIMPICCIOLISCI" },
    { "ROLL", "ROULER", "ROLLEN", "RODAR", "ROTOLA" },
    { "Deepwood Shrine", "Temple de la Forêt", "Wald-Schrein", "Sepulcro del bosque", "Tempietto della Foresta" },
    { "Cave of Flames", "Grotte de Feu", "Höhle der Flamme", "Cueva de las llamas", "Grotta del Fuoco" },
    { "Fortress of Winds", "Forteresse du Vent", "Tor des Windes", "Arco de los vientos", "Bastioni di Maestrale" },
    { "Temple of Droplets", "Temple de l'Eau", "Tempel des Tropfens", "Templo de las aguas", "Tempio delle Gocce" },
    { "Palace of Winds", "Palais du Vent", "Palast des Windes", "Palacio de los vientos", "Palazzo dell'Aria" },
    { "Dark Hyrule Castle", "Château des Ténèbres", "Dunkles Schloss Hyrule", "Castillo de Hyrule tenebroso", "Castello di Hyrule nelle tenebre" },
    { "THE GAME SLOWS DOWN IF IT CANNOT KEEP UP", "LE JEU RALENTIT S'IL NE SUIT PAS", "DAS SPIEL WIRD LANGSAMER, WENN ES NICHT MITKOMMT", "EL JUEGO VA MÁS LENTO SI NO PUEDE MÁS", "IL GIOCO RALLENTA SE NON REGGE" },
    { "30 FPS SHOWN, GAME AT FULL SPEED", "30 FPS AFFICHÉS, JEU À VITESSE NORMALE", "30 FPS ANGEZEIGT, SPIEL VOLLE GESCHWINDIGKEIT", "30 FPS MOSTRADOS, JUEGO A VELOCIDAD NORMAL", "30 FPS MOSTRATI, GIOCO A VELOCITÀ NORMALE" },
    { "CHANNEL", "CANAL", "KANAL", "CANAL", "CANALE" },
    { "PRE-RELEASE", "BÊTA", "BETA", "BETA", "BETA" },
    { "STABLE", "STABLE", "STABIL", "ESTABLE", "STABILE" },
    { "NO RELEASE SELECTED", "AUCUNE VERSION CHOISIE", "KEINE VERSION GEWÄHLT", "SIN VERSIÓN ELEGIDA", "NESSUNA VERSIONE" },
    { "DOWNLOAD AND INSTALL", "TÉLÉCHARGER ET INSTALLER", "LADEN UND INSTALLIEREN", "DESCARGAR E INSTALAR", "SCARICA E INSTALLA" },
    { "CHECK FOR UPDATE", "CHERCHER UNE MISE À JOUR", "NACH UPDATE SUCHEN", "BUSCAR ACTUALIZACIÓN", "CERCA AGGIORNAMENTI" },
    { "UNSAVED PROGRESS IS LOST", "PROGRESSION NON SAUVÉE PERDUE", "UNGESPEICHERTES GEHT VERLOREN", "SE PIERDE LO NO GUARDADO", "PROGRESSI NON SALVATI PERSI" },
    { "CHANGELOG ON TOP SCREEN", "NOTES SUR L'ÉCRAN HAUT", "NOTIZEN AUF DEM OBEREN BILDSCHIRM", "NOTAS EN LA PANTALLA SUPERIOR", "NOTE NELLO SCHERMO ALTO" },
    { "TAP RELEASE FOR CHANGELOG", "TOUCHER UNE VERSION: NOTES", "VERSION ANTIPPEN: NOTIZEN", "TOCA UNA VERSIÓN PARA NOTAS", "TOCCA UNA VERSIONE PER NOTE" },
    { "PREV", "PRÉC.", "ZURÜCK", "ANT.", "PREC." },
    { "NEXT", "SUIV.", "WEITER", "SIG.", "SUCC." },
    { "INSTALL UPDATE", "INSTALLER", "INSTALLIEREN", "INSTALAR", "INSTALLA" },
    { "PLEASE WAIT", "PATIENTEZ", "BITTE WARTEN", "ESPERA", "ATTENDI" },
    { "DOWNLOAD UPDATE", "TÉLÉCHARGER", "HERUNTERLADEN", "DESCARGAR", "SCARICA" },
    { "UPDATE INSTALLED", "MISE À JOUR INSTALLÉE", "UPDATE INSTALLIERT", "ACTUALIZACIÓN INSTALADA", "AGGIORNAMENTO INSTALLATO" },
    { "CHANGELOG", "NOTES", "ÄNDERUNGEN", "NOVEDADES", "NOVITÀ" },
    { "PAGE", "PAGE", "SEITE", "PÁGINA", "PAGINA" },
    { "OF", "SUR", "VON", "DE", "DI" },
    { "INSTALLED", "INSTALLÉE", "INSTALLIERT", "INSTALADA", "INSTALLATA" },
    { "SELECT A RELEASE BELOW TO READ ITS CHANGELOG.", "CHOISISSEZ UNE VERSION CI-DESSOUS POUR LIRE LES NOTES.", "WÄHLE UNTEN EINE VERSION FÜR DIE ÄNDERUNGEN.", "ELIGE ABAJO UNA VERSIÓN PARA VER LAS NOVEDADES.", "SCEGLI SOTTO UNA VERSIONE PER LEGGERE LE NOTE." },
    { "CHOOSE STABLE OR PRE-RELEASE TO CHECK THAT CHANNEL.", "CHOISISSEZ STABLE OU BÊTA POUR CE CANAL.", "WÄHLE STABIL ODER BETA FÜR DEN KANAL.", "ELIGE ESTABLE O BETA PARA ESE CANAL.", "SCEGLI STABILE O BETA PER QUEL CANALE." },
    { "SD CARD WRITE FAILED", "ÉCRITURE CARTE SD ÉCHOUÉE", "SD-SCHREIBFEHLER", "ERROR ESCRIBIENDO EN SD", "SCRITTURA SD FALLITA" },
    { "NO WI-FI CONNECTION", "PAS DE CONNEXION WI-FI", "KEINE WLAN-VERBINDUNG", "SIN CONEXIÓN WI-FI", "NESSUNA CONNESSIONE WI-FI" },
    { "TLS SERVICE FAILED", "SERVICE TLS ÉCHOUÉ", "TLS-DIENST FEHLGESCHLAGEN", "FALLÓ EL SERVICIO TLS", "SERVIZIO TLS FALLITO" },
    { "INVALID RELEASE DATA", "DONNÉES DE VERSION INVALIDES", "UNGÜLTIGE VERSIONSDATEN", "DATOS DE VERSIÓN NO VÁLIDOS", "DATI VERSIONE NON VALIDI" },
    { "NO PRE-RELEASE AVAILABLE", "AUCUNE BÊTA DISPONIBLE", "KEINE BETA VERFÜGBAR", "NO HAY BETA DISPONIBLE", "NESSUNA BETA DISPONIBILE" },
    { "UPDATE AVAILABLE", "MISE À JOUR DISPONIBLE", "UPDATE VERFÜGBAR", "ACTUALIZACIÓN DISPONIBLE", "AGGIORNAMENTO DISPONIBILE" },
    { "YOU ARE UP TO DATE", "VOUS ÊTES À JOUR", "SIE SIND AKTUELL", "ESTÁS AL DÍA", "SEI AGGIORNATO" },
    { "NOT ENOUGH SD SPACE", "ESPACE SD INSUFFISANT", "NICHT GENUG SD-SPEICHER", "SD SIN ESPACIO SUFICIENTE", "SPAZIO SD INSUFFICIENTE" },
    { "3DSX LAUNCH PATH UNKNOWN", "CHEMIN 3DSX INCONNU", "3DSX-PFAD UNBEKANNT", "RUTA 3DSX DESCONOCIDA", "PERCORSO 3DSX IGNOTO" },
    { "CANNOT WRITE TO SD CARD", "ÉCRITURE SD IMPOSSIBLE", "SD NICHT BESCHREIBBAR", "NO SE PUEDE ESCRIBIR EN SD", "IMPOSSIBILE SCRIVERE SU SD" },
    { "VERIFYING DOWNLOAD", "VÉRIFICATION", "DOWNLOAD WIRD GEPRÜFT", "VERIFICANDO DESCARGA", "VERIFICA DOWNLOAD" },
    { "DOWNLOAD CHECK FAILED", "VÉRIFICATION ÉCHOUÉE", "PRÜFUNG FEHLGESCHLAGEN", "FALLÓ LA VERIFICACIÓN", "VERIFICA FALLITA" },
    { "INSTALLING UPDATE", "INSTALLATION EN COURS", "UPDATE WIRD INSTALLIERT", "INSTALANDO ACTUALIZACIÓN", "INSTALLAZIONE IN CORSO" },
    { "INSTALLATION FAILED", "INSTALLATION ÉCHOUÉE", "INSTALLATION FEHLGESCHLAGEN", "FALLÓ LA INSTALACIÓN", "INSTALLAZIONE FALLITA" },
    { "UPDATE CANCELLED", "MISE À JOUR ANNULÉE", "UPDATE ABGEBROCHEN", "ACTUALIZACIÓN CANCELADA", "AGGIORNAMENTO ANNULLATO" },
    { "CONNECTION FAILED - RETRY", "CONNEXION ÉCHOUÉE - RÉESSAYER", "VERBINDUNG FEHLGESCHLAGEN - NOCHMAL", "CONEXIÓN FALLIDA - REINTENTA", "CONNESSIONE FALLITA - RIPROVA" },
    { "DOWNLOADING UPDATE", "TÉLÉCHARGEMENT", "UPDATE WIRD GELADEN", "DESCARGANDO ACTUALIZACIÓN", "DOWNLOAD IN CORSO" },
    { "CHECKING FOR UPDATES", "RECHERCHE EN COURS", "SUCHE NACH UPDATES", "BUSCANDO ACTUALIZACIONES", "RICERCA AGGIORNAMENTI" },
    { "CANNOT START UPDATE", "IMPOSSIBLE DE LANCER", "UPDATE NICHT STARTBAR", "NO SE PUEDE INICIAR", "IMPOSSIBILE AVVIARE" },
    { "CANNOT SAVE UPDATE CHANNEL", "CANAL NON SAUVEGARDÉ", "KANAL NICHT GESPEICHERT", "NO SE GUARDÓ EL CANAL", "CANALE NON SALVATO" },
};

#define ENTRY_COUNT ((int)(sizeof(kEntries) / sizeof(kEntries[0])))

/* Written by the main thread when the snapshot is built, read by the
 * bottom-screen and updater threads; a single byte, so a stale read only
 * means one frame of the previous language. */
static volatile uint8_t sLanguage = PORT_TEXT_EN;

void Port_Text_SetLanguage(int publicLanguage) {
    /* include/main.h: JP=0, EN=1, FR=2, DE=3, ES=4, IT=5. */
    PortTextLanguage lang = PORT_TEXT_EN;
    if (publicLanguage >= 2 && publicLanguage <= 5) {
        lang = (PortTextLanguage)(publicLanguage - 1);
    }
    sLanguage = (uint8_t)lang;
}

PortTextLanguage Port_Text_GetLanguage(void) {
    return (PortTextLanguage)sLanguage;
}

const char* Port_Text_TrFor(PortTextLanguage lang, const char* english) {
    if (english == NULL || lang <= PORT_TEXT_EN || lang >= PORT_TEXT_COUNT) {
        return english;
    }
    for (int i = 0; i < ENTRY_COUNT; i++) {
        const char* key = kEntries[i][PORT_TEXT_EN];
        if (key[0] == english[0] && strcmp(key, english) == 0) {
            return kEntries[i][lang];
        }
    }
    return english;
}

const char* Port_Text_Tr(const char* english) {
    return Port_Text_TrFor((PortTextLanguage)sLanguage, english);
}

int Port_Text_EntryCount(void) {
    return ENTRY_COUNT;
}

const char* Port_Text_EntryAt(int index, PortTextLanguage lang) {
    if (index < 0 || index >= ENTRY_COUNT || lang < 0 || lang >= PORT_TEXT_COUNT) {
        return NULL;
    }
    return kEntries[index][lang];
}

uint8_t Port_Text_Utf8Next(const char** p) {
    const uint8_t* s = (const uint8_t*)*p;
    uint32_t cp;
    if (s[0] < 0x80) {
        if (s[0] != 0) {
            (*p)++;
        }
        return s[0];
    }
    if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *p += 2;
        return cp < 0x100 ? (uint8_t)cp : (uint8_t)'?';
    }
    /* 3/4-byte sequences and stray bytes: skip the lead and its
     * continuation bytes, draw one '?'. */
    (*p)++;
    while ((**(const uint8_t**)p & 0xC0) == 0x80) {
        (*p)++;
    }
    return (uint8_t)'?';
}

char Port_Text_BaseLetter(uint8_t code) {
    static const char kLatin1Base[64] = {
        /* 0xC0 */ 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'C', 'E', 'E', 'E', 'E', 'I', 'I', 'I', 'I',
        /* 0xD0 */ 'D', 'N', 'O', 'O', 'O', 'O', 'O', 0,   'O', 'U', 'U', 'U', 'U', 'Y', 0,   0,
        /* 0xE0 */ 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i',
        /* 0xF0 */ 'd', 'n', 'o', 'o', 'o', 'o', 'o', 0,   'o', 'u', 'u', 'u', 'u', 'y', 0,   'y',
    };
    if (code >= 0xC0) {
        return kLatin1Base[code - 0xC0];
    }
    return 0;
}
