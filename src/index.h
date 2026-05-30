const char MAIN_page[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<body>
<table style="height: 250px; margin-left: auto; margin-right: auto;" width="290">
<tbody>
<tr>
<td style="width: 137px;">Name</td>
<td style="width: 137px;"><span id="IOName">???</span></td>
</tr>
<tr>
<td style="width: 137px;">Pin</td>
<td style="width: 137px;"><span id="IOPin">???</span></td>
</tr>
<tr>
<td style="width: 137px;">Timeout</td>
<td style="width: 137px;"><span id="IOTimeout">???</span></td>
</tr>
<tr>
<td style="width: 137px;">Data Timer</td>
<td style="width: 137px;"><span id="IODataTimer">???</span></td>
</tr>
<tr>
<td style="width: 137px;">Type</td>
<td style="width: 137px;"><span id="IOType">???</span></td>
</tr>
<tr>
<td style="width: 137px;">Default Value</td>
<td style="width: 137px;"><span id="IODefaultValue">???</span></td>
</tr>
<tr>
<td style="width: 137px;">Value</td>
<td style="width: 137px;"><span id="IOValue">???</span></td>
</tr>
<tr>
<td style="width: 137px;">Invert</td>
<td style="width: 137px;"><span id="IOInvert">???</span></td>
</tr>
</tbody>
</table>
<p style="text-align: center;"><input id="IOIndex" name="Index" type="text" value="0" /> <button type="button">Ok</button></p>
<p style="text-align: center;">&nbsp;</p>

<script>
setInterval(function() {
  getData();
}, 500);

function getData() {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      var obj = JSON.parse(this.responseText);
      document.getElementById("IOName").innerHTML = obj.Name;
 document.getElementById("IOPin").innerHTML = obj.Pin;
 document.getElementById("IOTimeout").innerHTML = obj.Timeout;
 document.getElementById("IODataTimer").innerHTML = obj.DataTimer;
 document.getElementById("IOType").innerHTML = obj.Type;
 document.getElementById("IODefaultValue").innerHTML = obj.DefaultValue;
 document.getElementById("IOValue").innerHTML = obj.Value;
 document.getElementById("IOInvert").innerHTML = obj.Invert;
    }
  };
 
  var idx = document.getElementById("IOIndex").value;
  xhttp.open("GET", "getData?IOIndex="+idx, true);
  xhttp.send();
}
</script>
</body>
</html>
)=====";
