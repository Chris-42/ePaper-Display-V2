<?PHP

$device_db = array(
    "3C:71:BF:44:28:94" => "ePaper7Display",
    "10:91:A8:03:CF:70" => "ePaper7DisplayV2"
);

header('Content-type: text/plain; charset=utf8', true);

function check_header($name, $value = false) {
    if(!isset($_SERVER[$name])) {
        return false;
    }
    if($value && $_SERVER[$name] != $value) {
        return false;
    }
    return true;
}

function sendFile($path) {
    header($_SERVER["SERVER_PROTOCOL"].' 200 OK', true, 200);
    header('Content-Type: application/octet-stream', true);
    header('Content-Disposition: attachment; filename='.basename($path));
    header('Content-Length: '.filesize($path), true);
    header('x-MD5: '.md5_file($path), true);
    readfile($path);
}

if(!check_header('HTTP_USER_AGENT', 'ESP32-http-Update')) {
    header($_SERVER["SERVER_PROTOCOL"].' 403 Forbidden', true, 403);
    echo "only for ESP32 updater!\n";
    error_log("update req without headers");
    exit();
}


if( !check_header('HTTP_X_ESP32_STA_MAC') ||
    !check_header('HTTP_X_ESP32_AP_MAC') ||
    #!check_header('HTTP_X_ESP32_FREE_SPACE') ||
    #!check_header('HTTP_X_ESP32_SKETCH_SIZE') ||
    #!check_header('HTTP_X_ESP32_SKETCH_MD5') ||
    #!check_header('HTTP_X_ESP32_CHIP_SIZE') ||
    !check_header('HTTP_X_ESP32_MODE') ||
    !check_header('HTTP_X_ESP32_VERSION') ||
    !check_header('HTTP_X_ESP32_SDK_VERSION')
) {
    header($_SERVER["SERVER_PROTOCOL"].' 403 Forbidden', true, 403);
    echo "only for ESP32 updater! (header)\n";
    error_log("update req with wrong headers");
    exit();
}

if(!isset($device_db[$_SERVER['HTTP_X_ESP32_STA_MAC']])) {
    header($_SERVER["SERVER_PROTOCOL"].' 500 ESP MAC not configured for updates', true, 500);
    error_log("update req with unconfigured mac " . $_SERVER['HTTP_X_ESP32_STA_MAC']);
    exit();
}

$software = $device_db[$_SERVER['HTTP_X_ESP32_STA_MAC']];

$softwareversion = -1;

$dh = @opendir("./bin");
while($file = readdir($dh)){
    if(($file == ".") || ($file == "..")) {
        continue;
    }
    list($filename,$fileversion_l,$fileversion_h,) = explode(".", $file, 4);
    $fileversion = floatval($fileversion_l . "." . $fileversion_h);
    #error_log("file " .$filename . " with v:" .$fileversion );
    if($software == $filename) {
        #error_log("match " .$filename );
        if($softwareversion < $fileversion) {
            $softwareversion = $fileversion;
            #error_log("stored v:" .$fileversion );
        }
    }
}

if($softwareversion == -1) {
    header($_SERVER["SERVER_PROTOCOL"].' 500 no software in repo for update', true, 500);
}

$current_version = floatval($_SERVER['HTTP_X_ESP32_VERSION']);

error_log("update req to " .$software . " v" . $softwareversion . " with installed v:" .$current_version );

if(0 or $softwareversion > $current_version) {

    $localBinary = "./bin/" . $software . "." . $softwareversion . ".bin";

    if(file_exists($localBinary)) {
        error_log("send update");
        sendFile($localBinary);
        exit;
    } else {
        header($_SERVER["SERVER_PROTOCOL"].' 304 Not Modified', true, 304);
    }
}
header($_SERVER["SERVER_PROTOCOL"].' 304 ot Modified', true, 304);

?>
