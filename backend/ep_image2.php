<?php

if(isset($_SERVER["HTTP_DISPLAYNAME"])) {
    $displayname = $_SERVER["HTTP_DISPLAYNAME"];
} else {
    if(isset($_REQUEST["DISPLAYNAME"])) {
        $displayname = $_REQUEST["DISPLAYNAME"];
    } else {
        $displayname = "dummy";
    }
}
if(isset($_SERVER["HTTP_VERSION"])) {
    $versionstr = $_SERVER["HTTP_VERSION"];
} else {
    $versionstr = "";
}
if(isset($_SERVER["HTTP_IF_MODIFIED_SINCE"])) {
    $imagedatestr = $_SERVER["HTTP_IF_MODIFIED_SINCE"];
} else {
    $imagedatestr = "";
}
if(isset($_SERVER["HTTP_CONTENTHASH"])) {
    $contenthash = $_SERVER["HTTP_CONTENTHASH"];
} else {
    $contenthash = "";
}
if(isset($_SERVER["HTTP_NEXTSCHEDULE"])) {
        $next_schedule = $_SERVER["HTTP_NEXTSCHEDULE"];
} else {
        $next_schedule = "";
}
if(isset($_SERVER["HTTP_WIFIFAIL"])) {
    $wifi_fail = $_SERVER["HTTP_WIFIFAIL"];
} else {
    $wifi_fail = -1;
}
if(isset($_SERVER["HTTP_WIFITIME"])) {
    $wifi_time = $_SERVER["HTTP_WIFITIME"];
} else {
    $wifi_time = -1;
}
if(isset($_SERVER["HTTP_BATTERIEPOWER"])) {
    $voltage = $_SERVER["HTTP_BATTERIEPOWER"];
} else {
    $voltage = 0;
}
if(isset($_SERVER["HTTP_RUNMILLIS"])) {
    $run_millis = $_SERVER["HTTP_RUNMILLIS"];
} else {
    $run_millis = 0;
}
if(isset($_SERVER["HTTP_WAKEUP"])) {
    $wakeup_by = $_SERVER["HTTP_WAKEUP"];
} else {
    $wakeup_by = "?";
}

#header($_SERVER["SERVER_PROTOCOL"].' 304 ot Modified', true, 304);
#$imagedatestr = "Thu, 01 Jan 1970 01:00:00 GMT";

#$fontR = "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf";
#$fontB = "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans-Bold.ttf";
#$fontL = "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSansCondensed.ttf";
#$fontR = "/usr/share/fonts/truetype/ubuntu-font-family/Ubuntu-R.ttf";
#$fontB = "/usr/share/fonts/truetype/ubuntu-font-family/Ubuntu-B.ttf";
#$fontL = "/usr/share/fonts/truetype/ubuntu-font-family/Ubuntu-L.ttf";
#$fontR = "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf";
#$fontB = "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf";
#$fontR = "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf";
$fontR = "/var/www/fonts/ubuntu-font-family/Ubuntu-R.ttf";
$fontB = "/var/www/fonts/ubuntu-font-family/Ubuntu-B.ttf";
$fontL = "/var/www/fonts/ubuntu-font-family/Ubuntu-L.ttf";



$dbHost="*";
$dbName="*";
$dbUser="*";
$dbPassword = "*";

$connection;

function mysqlcon() {
    global $dbHost, $dbUser, $dbPassword, $dbName;
    global $connection;

    if(!$connection = mysqli_connect($dbHost, $dbUser, $dbPassword, $dbName)) {
        echo "Keine Verbindung moeglich: " . mysqli_connect_error();
        error_log("Keine Verbindung moeglich: " . mysqli_connect_error());
        die;
    }
}

mysqlcon();

function my_mysql_query($sql) {
    global $connection;
    $ergebnis = mysqli_query($connection, $sql);

    if (mysqli_errno($connection)) {
        echo "Fehler in $sql:" . mysqli_error();
        error_log( "Fehler in $sql:" . mysqli_error());
        die;
    }
    return $ergebnis;
}

$SQL = "
    SELECT to_days(date) - to_days(now()) days, name, who, DATE_FORMAT(`date`,'%H:%i') time from Termine having days < 15 and days >= 0 order by days
    limit 5;
";

define('TIMEZONE', 'Europe/Paris');
date_default_timezone_set(TIMEZONE);
$now = new DateTime();
$mins = $now->getOffset() / 60;
$sgn = ($mins < 0 ? -1 : 1);
$mins = abs($mins);
$hrs = floor($mins / 60);
$mins -= $hrs * 60;
$offset = sprintf('%+d:%02d', $hrs*$sgn, $mins);
my_mysql_query("SET time_zone='$offset';");
#echo $offset;

$result = my_mysql_query($SQL);
$rows = mysqli_num_rows($result);
$arr = array();
setlocale(LC_TIME, "de_DE.UTF-8");
while($row = mysqli_fetch_object($result)) {
    $a = array();
    foreach($row as $name => $value) {
        $a[$name] = utf8_decode($value);
    }
    if($a['who'] != "waste") {
        $a['name'] .= " (" . $a['time'] . ")";
    }
    if($a['days'] == 0) {
        $a['days'] = "Heute";
    } elseif($a['days'] == 1) {
        $a['days'] = "Morgen";
    } else {
        $a['days'] = $a['days'] . " Tage";
    }
    if($a['who'] == "cp") {
        $a['who'] = "*:";
    } elseif ( $a['who'] == "bp") {
        $a['who'] = "*:";
    } elseif ( $a['who'] == "waste") {
        $a['who'] = "Müll:";
    }
    $arr[] = $a;
}

$curl = curl_init();
curl_setopt($curl, CURLOPT_URL, "http://localhost:18080/rest/items/");
curl_setopt($curl, CURLOPT_RETURNTRANSFER, 1);
$schwimmbadfenster = curl_exec($curl);
curl_setopt($curl, CURLOPT_URL, "http://localhost:18080/rest/items/");
curl_setopt($curl, CURLOPT_RETURNTRANSFER, 1);
$terrassentuer = curl_exec($curl);
curl_close($curl);
error_log("terrassentuer: " . $terrassentuer);

$scheduletimestamp = strtotime($next_schedule);
$dt = new \DateTime(null, new \DateTimeZone(TIMEZONE));
$todaystr = $dt->format("d.m.Y");
$dt_utc = new \DateTime(null, new \DateTimeZone("UTC"));
#$todaystr = $dt->format("d.m.Y");
$hour = date("G");
$day = date("N");
if($hour > 22) {
  $dt_utc->modify("+24 hour");
  $dt_utc->setTime(5, 44);
} elseif($hour < 7) {
  $dt_utc->setTime(5, 44);
} else {
  $dt_utc->modify("+90 minute");
}
$datestr = $dt_utc->format("D, d M Y H:i:s \G\M\T");
$delta_schedule = abs($dt->getTimestamp() - $scheduletimestamp);


error_log("Display: " .$displayname);
error_log("Version: " .$versionstr);
error_log("wakeup: " . $wakeup_by);
error_log("Mod planned NextSchedule: " .$next_schedule);
error_log("Server planned NextSchedule: " .$datestr);
error_log("delta Schedule: " .$delta_schedule);
$hash = md5("1" . json_encode($arr) . $terrassentuer . $schwimmbadfenster . $todaystr);
error_log(json_encode($arr) . $todaystr);
error_log("Batterie: " . $voltage);
error_log("WiFiFail: " . $wifi_fail);
error_log("WiFiTime: " . $wifi_time);
error_log("RunMillis: " . $run_millis);
error_log("ContentHash: " . $contenthash);
if(1 && ($contenthash == $hash) && (($delta_schedule < 1500000000) || ($delta_schedule > 300))) {
    error_log("no change, 304");
    header($_SERVER["SERVER_PROTOCOL"].' 304 ot Modified', true, 304);
    exit;
}
error_log("new ContentHash: " . $hash);
header("ContentHash: " . $hash);

$imagetime = strtotime($imagedatestr);
$now = time();
$diff = $now - $imagetime;
error_log("imagetimestr " . $imagedatestr);
error_log("image age:" . $diff);
#error_log("imagetime " . $imagetime->format("D, d M Y H:i:s \G\M\T"));

header("Sleep: " . 60);
#error_log("NextSchedule: " . $datestr);

header("NewVersion: 1.11");


error_log("creating image");
header( "HTTP/1.0 200 OK" );
header ("Content-type: image/bmp");
#header ("Content-type: image/png");
#imagecopyresampled($im, $im0, 0, 0, 0, 0, 800, 480, imagesx($im0), imagesy($im0));
#$im = @ImageCreateTrueColor (800, 480)
$im = ImageCreateFromPNG($displayname . ".png")
or die ("Kann keinen neuen GD-Bild-Stream erzeugen");
$background_color = ImageColorAllocate ($im, 255, 255, 255);
$text_color = ImageColorAllocate ($im, 0, 0, 0);
if($displayname == "Mirror") {
    imagettftext ($im , 40 , 90 , 130 , 320 , $text_color , $fontR , $todaystr );
    if($rows) {
        for($i = 0; $i < $rows; $i++) {
            imagettftext ($im , 32 , 90 , 180+85*$i , 380, $text_color , $fontB, $arr[$i]['days'] );
            imagettftext ($im , 32 , 90 , 180+85*$i , 190, $text_color , $fontB , $arr[$i]['who'] );
            imagettftext ($im , 32 , 90 , 220+85*$i , 340, $text_color , $fontB , $arr[$i]['name'] );
        }
    }
    $bat_bar = intval(($voltage - 2850) / 6);
    if($bat_bar > 30) {
        $bat_bar = 30;
    }
    ImageRectangle($im, 627, 2, 636, 34, $text_color);
    if($bat_bar > 0) {
        ImageFilledRectangle($im, 628, 33 - $bat_bar, 635, 33, $text_color);
    }
} else {
    ImageRectangle($im, 0, 0, 639, 383, $text_color);
    #imagettftext ($im , 40 , 0 , 31 , 70 , $text_color , $fontB , "Raum " . $displayname);
    imagettftext ($im , 40 , 0 , 31 , 60 , $text_color , $fontB , "Casa Port");
    ImageFilledRectangle($im, 370, 20, 638, 70, $background_color);
    imagettftext ($im , 32 , 0 , 410 , 60 , $text_color , $fontR , $todaystr );

    ImageFilledRectangle($im, 0, 80, 639, 82, $text_color);

    if(!$rows) {
    # empty first line
        imagettftext ($im , 24 , 0 , 31 , 140 , $text_color , $fontR, "Heute" );
        ImageRectangle($im, 175, 110, 175, 220, $text_color);
    
        imagettftext ($im , 24 , 0 , 190 , 180 , $text_color , $fontR , "Nix los." );
        imagettftext ($im , 16 , 0 , 190 , 213 , $text_color , $fontR , "Herzlich willkommen!" );
    } else {
        for($i = 0; $i < $rows; $i++) {
    # only first line
            imagettftext ($im , 24 , 0 , 21 , 130+35*$i , $text_color , $fontB, $arr[$i]['days'] );
            #ImageRectangle($im, 175, 110, 175, 220, $text_color);
    
            imagettftext ($im , 24 , 0 , 140 , 130+35*$i , $text_color , $fontB , $arr[$i]['who'] );
            imagettftext ($im , 24 , 0 , 290 , 130+35*$i , $text_color , $fontB , $arr[$i]['name'] );
            #imagettftext ($im , 24 , 0 , 190 , 180 , $text_color , $fontR , utf8_decode($arr[0]['title'] ));
            #imagettftext ($im , 16 , 0 , 190 , 213 , $text_color , $fontR , utf8_decode($arr[0]['attendies'] ));
        }
    }

    if($terrassentuer == "OPEN") {
        $icon1 = imagecreatefrompng('tuer_offen.png');
        imagecopy($im, $icon1, 590, 250, 0, 0, 50, 50);
    }
    
    if($schwimmbadfenster == "OPEN") {
        $icon1 = imagecreatefrompng('tuer_offen.png');
        imagecopy($im, $icon1, 590, 300, 0, 0, 50, 50);
    }
}

#imagePNG($im);
$l =  imagesx($im) * imagesy($im) / 8 + 62;
header("Content-Length: " . $l);
echo(imageBMP2($im, ($displayname == "Mirror")));
exit();


function imageBMP2($im, $negate) {
    $bits = "";
    $bytes = "";
    $pixelcount = 0;
    #
    # file header
    $bytes .= pack('A*', "BM"); #uint16_t signature;
    $bytes .= pack('V', imagesx($im) * imagesy($im) / 8 + 54 + 8); #uint32_t file_size;
    $bytes .= pack('V', 0); #uint32_t reserved;
    $bytes .= pack('V', 54+8); #uint32_t image_offset;

    #picture header
    $bytes .= pack('V', 40); #uint32_t header_size;
    $bytes .= pack('V', imagesx($im)); #int32_t image_width;
    $bytes .= pack('V', 0 - imagesy($im)); #int32_t image_height;
    $bytes .= pack('v', 1); #uint16_t color_planes;
    $bytes .= pack('v', 1); #uint16_t bits_per_pixel;
    $bytes .= pack('V', 0); #uint32_t compression_method;
    $bytes .= pack('V', imagesx($im) * imagesy($im) / 8); #uint32_t image_size;
    $bytes .= pack('V', 0); #uint32_t horizontal_resolution;
    $bytes .= pack('V', 0); #uint32_t vertical_resolution;
    $bytes .= pack('V', 0); #uint32_t colors_in_palette;
    $bytes .= pack('V', 0); #uint32_t important_colors;

    # unknown fill bytes
    $bytes .= pack('V', 0); #uint32_t ?;
    $bytes .= pack('V', 0xffffff); #uint32_t ?;

    # image data
    for ($y = 0; $y < imagesy($im); $y++) {
        for ($x = 0; $x < imagesx($im); $x++) {

            $rgb = imagecolorat($im, $x, $y);
            $r = ($rgb >> 16) & 0xFF;
            $g = ($rgb >> 8 ) & 0xFF;
            $b = $rgb & 0xFF;
            $gray = ($r + $g + $b) / 3;

            if($negate) {
                if ($gray < 0x0e) {
                    $bits .= "1";
                }else {
                    $bits .= "0";
                }
            } else {
                if ($gray > 0xF1) {
                    $bits .= "1";
                }else {
                    $bits .= "0";
                }
            }
            $pixelcount++;
            if ($pixelcount % 8 == 0) {
                $bytes .= pack('H*', str_pad(base_convert($bits, 2, 16),2, "0", STR_PAD_LEFT));
                $bits = "";
            }
        }
    }
    return $bytes;
}
?>
