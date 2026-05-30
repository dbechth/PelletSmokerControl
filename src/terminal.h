const char terminal_page[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<body>

<div id="demo">
<h1>Terminal:</h1>
</div>

<div>
  <span id="Terminal"></span><br>
</div>
<script>
setInterval(function() {
  // Call a function repetatively
  getData();
}, 100); //100ms update rate

function getData() {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("Terminal").innerHTML =  this.responseText + document.getElementById("Terminal").innerHTML;
    }
  };
  xhttp.open("GET", "handleTerminal", true);
  xhttp.send();
}
</script>
</body>
</html>
)=====";
