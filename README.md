# pt682c-TV-App-for-Mac
PT682C TV v0.4 — escaneo de programas y lista persistente
=======================================================
Paquete independiente basado en los fuentes que enviaste. No reemplaza tu carpeta
funcional. No incluye firmware; utiliza el archivo verificado ya instalado en:
~/pt682c-diagnostic/firmware/dvb-demod-mxl692.fw
Puede cambiarse con la variable PT682C_FIRMWARE.

REQUISITOS
macOS, herramientas clang de Apple, Homebrew libusb y VLC instalado.
No necesita Python ni instalar otras dependencias nuevas.

INICIO
1. Descomprime y conserva toda la carpeta, incluidas core/ y data/.
2. Cierra receptores anteriores con q y espera SESSION CLOSED. Cierra VLC.
3. Abre Terminal en esta nueva carpeta.
4. Si el PT682C sigue conectado con firmware activo: bash tv.sh
5. Si acabas de conectarlo fisicamente o lo has reconectado: bash tv.sh --cold
6. Elige 1 (lista guardada) o 2 (nuevo escaneo); confirma INICIAR.

El inicio normal conserva la reapertura en caliente probada: reset del puente USB,
consulta de firmware activo y reutilizacion. Si no responde, se detiene, no recarga
firmware automaticamente. --cold utiliza la inicializacion en frio; no lo uses
sobre firmware activo sin reconectar fisicamente antes.

LISTA INICIAL Y PERSISTENCIA
Incluimos tu lista funcional de 12 entradas como data/last-scan.tsv, marcada con
un comentario de origen historico. No representa resultados de un nuevo escaneo.
Tambien se conserva una copia inmutable de referencia en data/legacy-2025.tsv.
Un escaneo guardado reemplaza data/last-scan.tsv y crea un respaldo .bak-fecha-pid.
No se fusionan automaticamente resultados viejos y nuevos.
No vuelvas a copiar el data/ del ZIP sobre tu carpeta despues de guardar escaneos.
Al abrir de nuevo esta MISMA carpeta, opcion 1 carga la ultima lista guardada.

NUEVO ESCANEO
Recorre RF 2-51 excepto 37: 49 frecuencias del plan de centros usado anteriormente.
Esto no es un barrido continuo de todas las frecuencias RF ni soporta otros
estandares (DVB/ISDB/ATSC 3.0). Tampoco es una afirmacion de asignaciones locales.
Espera hasta unos 6 segundos para bloqueo ATSC+FEC por frecuencia. Si hay bloqueo,
recibe unos 5 segundos de MPEG-TS y busca PAT, PMT y TVCT/CVCT.
Solo agrega programas encontrados en PAT y con PMT valida y al menos un stream.
Comprueba CRC de secciones y continuidad; reconstruye secciones fragmentadas.
Obtiene nombre corto/numero virtual de VCT cuando esta disponible; en otro caso
usa RF N - Programa P. Los nombres cortos pueden diferir de los nombres comerciales.
Puede listar servicios de audio/datos y de acceso condicionado; descubrir una
entrada no prueba que sea television gratuita reproducible.
Ventanas limitadas, condiciones de antena o tablas ausentes pueden omitir servicios.
Por eso ofrece confirmacion GUARDAR y NO afirma encontrar toda emision posible.

Si no encuentra programas, hay error o se cancela con Ctrl-C, conserva la lista.
Con resultados, pide GUARDAR para reemplazarla; Enter conserva la anterior.
La escritura se hace a archivo temporal, luego respaldo y reemplazo por rename.
Despues carga la lista elegida y abre el menu sin cerrar el dispositivo USB.
El escaneo puede tardar varios minutos. Coloca la antena antes de empezar.

REPRODUCCION
Numero + Enter cambia canal, m muestra lista, q sale. Ctrl-C tambien sale.
Mismo multiplex: selecciona otro programa con VLC. Otra frecuencia: resintoniza.
Conserva la sesion USB. Reinicia solo el VLC hijo de este controlador por cambio,
como la version anterior (SIGTERM, y SIGKILL despues de 3 s si no termina).
Solo transmite UDP a 127.0.0.1:1234. No graba video.

ARCHIVOS
 data/last-scan.tsv — lista utilizada al abrir
 data/last-scan.tsv.bak-* — listas anteriores
 data/scan-report-*.csv — diagnostico de frecuencias y tablas
 logs/tv-*.txt — registro completo de ejecucion
 session-vlc-log.txt — registro acumulado de VLC
 build/ — ejecutables compilados localmente

VALIDACION Y LIMITES
Nueva integracion aun no validada con hardware real ni compilada en macOS aqui.
Se comprobaron sintaxis C (dos rutas con encabezado libusb de prueba), sintaxis bash,
y parser de tablas con ASan/UBSan en entorno de desarrollo.
Pruebas sinteticas: PAT/PMT/VCT, nombres UTF-16, VCT fragmentada, prefijo de basura,
CRC invalidos y respaldo/guardado/cancelacion con transporte simulado.
Esto no equivale a probar tu antena ni garantiza estabilidad prolongada.
Los fuentes base fueron los adjuntos pt682c_fwload-8.c, pt682c_scan_v2-12.c,
pt682c_session-17.c, pt682c_fwload_warm-7.c, pt682c_warm_get-18.h y
pt682c_reset_open-11.h. Se conservaron ambas rutas de arranque.

PRUEBAS LOCALES OPCIONALES SIN USB
 bash test-tables.sh
Requiere clang con sanitizers. Solo prueba el parser con TS artificiales incluidos.

LICENCIA
GPL-2.0 para las adaptaciones; atribuciones de Linux/MaxLinear/Brad Love/Max nibble
y del parche de Forest Crossman se conservan en los fuentes.
# PT682C-TV
# PT682C-TV
# PT682C-TV
