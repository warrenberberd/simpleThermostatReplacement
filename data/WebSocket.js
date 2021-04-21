//var connection = new WebSocket('ws://'+location.hostname+':81/', ['arduino']);

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

/*function sendRGB() {
    var r = document.getElementById('r').value**2/1023;
    var g = document.getElementById('g').value**2/1023;
    var b = document.getElementById('b').value**2/1023;
    
    var rgb = r << 20 | g << 10 | b;
    var rgbstr = '#'+ rgb.toString(16);    
    console.log('RGB: ' + rgbstr); 
    connection.send(rgbstr);
} */

