var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen    = onOpen;
    websocket.onclose   = onClose;
    websocket.onmessage = onMessage; // <-- add this line
}
function onOpen(event) {
    console.log('Connection opened');
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
}
function onMessage(event) {
    var state;
    var jsonObject;

    //console.log("DataType: " + typeof event.data);
    if(typeof event.data === "string" ){
        console.log("Received data string : ",event.data);
        //console.log(event.data);
        //create a JSON object
        jsonObject = JSON.parse(event.data);
    }else if(event.data instanceof ArrayBuffer ){
        var buffer = event.data;
        console.log("Received arraybuffer");
    }
    //console.log("JsonObject: ");
    //console.log(jsonObject);

    // Mise a jour de la temperature
    if(typeof jsonObject.temp !=="undefined"){
        document.getElementById('TEMP_DATA').innerHTML = jsonObject.temp;
    }
    // Mise a jour du voltage
    if(typeof jsonObject.voltage !=="undefined"){
        document.getElementById('VOLT_DATA').innerHTML = jsonObject.voltage;
    }
    // Mise a jour de l'état du relai
    if(typeof jsonObject.switch !=="undefined"){
        document.getElementById('SWITCH_DATA').innerHTML = jsonObject.switch;
    }
    // Mise a jour du thermostat
    if(typeof jsonObject.thermostat !=="undefined"){
        document.getElementById('THERMOSTAT').value = jsonObject.thermostat.replace(".",",");
    }
    // Mise a jour uptime
    if(typeof jsonObject.uptime !=="undefined"){
        document.getElementById('UPTIME_DATA').innerHTML = jsonObject.uptime;
    }
   
    //document.getElementById('SWITCH_DATA').innerHTML = state;
}
function onLoad(event) {
    initWebSocket();
    initButton();
}

function initButton() {
    document.getElementById('button').addEventListener('click', toggle);
    document.getElementById('THERMOSTAT').addEventListener('click', onClickThermostat);
}
function toggle(){
    var state=document.getElementById('SWITCH_DATA').innerHTML;

    console.log("Seding data string : toggle:",state);
    websocket.send("toggle:" + state);
}

function onClickThermostat(){
    var value=document.getElementById('THERMOSTAT').value;

    console.log("Seding data string : changeThermostat:",value);
    websocket.send("changeThermostat:" + value);
}

window.addEventListener('load', onLoad);

/*var connection = new WebSocket('ws://'+location.hostname+'/ws', ['arduino']);

connection.onopen = function () {
    connection.send('Connect ' + new Date());
};
connection.onerror = function (error) {
    console.log('WebSocket Error ', error);


};
connection.onmessage = function (e) {  
    console.log('Server: ', e.data);

    var elNameFromLogs="TEMP_DATA";
    var l = document.getElementById(elNameFromLogs);
    //var val=l.innerHTML;
    var val="";
    val+=e.data;
    l.innerHTML=val;

};
connection.onclose = function(){
    console.log('WebSocket connection closed');
};
*/
/*function sendRGB() {
    var r = document.getElementById('r').value**2/1023;
    var g = document.getElementById('g').value**2/1023;
    var b = document.getElementById('b').value**2/1023;
    
    var rgb = r << 20 | g << 10 | b;
    var rgbstr = '#'+ rgb.toString(16);    
    console.log('RGB: ' + rgbstr); 
    connection.send(rgbstr);
} */

