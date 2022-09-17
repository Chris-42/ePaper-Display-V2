# ePaper-Display-V2
Waveshare 7.5 ePaper display with XIAO-ESP32C3 board and 3Ah LiPo.<br>
Get an image from backend web server (BW-BMP or BW-PNG) and draw it if there is a change.<br>
Config is done via serial (USB).<br>
<p>
runtime calculation with PNG and without battery self discharge for best wifi conditions<br>
 sleep: 165mC/h = 0,046mAh<br>
 wakeup+check without redraw: 35mC = 0,0097mAh<br>
 wakeup+check+redraw: 200mC = 0,056mAh<br>
 battery capacity: 10000000mC = 3000mAh<br>
 lets use interval 5 min, redraw all once a hour<br>
 consumption per hour: 165mC + 11 * 35mC + 1 * 200mC = 750mC -> 10800000mC / 750mC/h = 14400h = 600d<br>
 consumption per hour: 0,046 + 11 * 0,0097mAh + 1 * 0,056mAh = 0,2087mAh -> 3000mAh / 0,2087mAh/h = 14374h = 598d<br>
 to get a nice formula we use do some rounding<br>
  n = wakeup / hour<br>
  m = redraws / hour<br>
  C = battery capacity in mC<br>
  c = battery capacity in mAh<br>
 to calculate hours from Coulomb<br>
 t = C / ((n - m) * 35 + m * 200 + 165) = C / ( n * 35 + (m+1) * 165 ) = C / 35 / (n + 5m + 5))<br>
  because 3600C = 1Ah -> C = c * 3600<br>
 t = c * 3600 / 35 / ( n + 5m + 5) ~ c * 100 / (n + 5m + 5)<br>
 to get days divide by 24 (just use 25)<br>
 t ~ c * 4 / (n + 5m + 5)<br>
 <br>
 so time in days is aprox c * 4 / (n + 5m + 5). keep in mind it is only for best WiFi conditions and fast web response.<br>
 for the example above (5 min wakeup, 1 refresh/h):<br>
 t = 12000 / (12 + 5 + 5) = 545d<br>
 <br>
 if the display refresh is twice (n = 12, m = 2):<br>
 t = 12000 / (12 + 10 + 5) = 324d<br>
 <br>
 wake 3 min + redraw once a hour:<br>
 t = 12000 / (20 + 5 + 5) = 400d<br>
<br>
wake 1 min + redraw once a hour:<br>
 t = 12000 / (60 + 5 + 5) = 171d<br>
 <br>
 wake 1 min + redraw twice a hour:<br>
 t = 12000 / (60 + 10 + 5) = 160d<br>
