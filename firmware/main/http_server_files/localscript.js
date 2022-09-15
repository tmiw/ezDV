//==========================================================================================
// Form state change
//==========================================================================================
var updateWifiFormState = function() 
{
    if ($("#wifiEnable").is(':checked'))
    {
        $(".wifi-enable-row").show(); 
    }
    else
    {
        $(".wifi-enable-row").hide();
    }
    
    if ($("#wifiMode").val() == 0)
    {
        // AP mode
        $("#wifiSecurityType").prop("disabled", false);
        $("#wifiChannel").prop("disabled", false);
    }
    else
    {
        // client mode
        $("#wifiSecurityType").prop("disabled", true);
        $("#wifiChannel").prop("disabled", true);
    }
    
    if ($("#wifiMode").val() == 0 && $("#wifiSecurityType").val() == 0)
    {
        // Open / client mode
        $("#wifiPassword").prop("disabled", true);
    }
    else
    {
        // Mode that requires encryption
        $("#wifiPassword").prop("disabled", false);
    }
};

var updateRadioFormState = function()
{
    if ($("#radioEnable").is(':checked'))
    {
        $(".radio-enable-row").show(); 
    }
    else
    {
        $(".radio-enable-row").hide();
    }
};

//==========================================================================================
// WebSocket handling
//==========================================================================================
var ws = null;
function wsConnect() 
{
  ws = new WebSocket("ws://" + location.hostname + "/ws");
  ws.onopen = function() { };
  ws.onmessage = function(e) 
  {
      var json = JSON.parse(e.data);
      
      if (json.type == "wifiInfo")
      {
          // Display current Wi-Fi settings
          $("#wifiEnable").prop("disabled", false);
          $("#wifiReset").prop("disabled", false);
          $("#wifiEnable").prop("checked", json.enabled);
          
          $("#wifiMode").val(json.mode);
          $("#wifiSecurityType").val(json.security);
          $("#wifiChannel").val(json.channel);
          $("#wifiSSID").val(json.ssid);
          $("#wifiPassword").val(json.password);      
          
          updateWifiFormState();    
      }
      else if (json.type == "wifiSaved")
      {
          if (json.success)
          {
              $("#wifiSuccessAlertRow").show();
          }
          else
          {
              $("#wifiFailAlertRow").show();
          }
      }
      else if (json.type == "radioInfo")
      {
          // Display current Wi-Fi settings
          $("#radioEnable").prop("disabled", false);
          $("#radioReset").prop("disabled", false);
          $("#radioEnable").prop("checked", json.enabled);
          
          $("#radioIP").val(json.host);
          $("#radioPort").val(json.port);
          $("#radioUsername").val(json.username);
          $("#radioPassword").val(json.password);
          
          updateRadioFormState();
      }
      else if (json.type == "radioSaved")
      {
          if (json.success)
          {
              $("#radioSuccessAlertRow").show();
          }
          else
          {
              $("#radioFailAlertRow").show();
          }
      }
  };

  ws.onclose = function(e) 
  {
    console.log('Socket is closed. Reconnect will be attempted in 1 second.', e.reason);
    setTimeout(function() {
      connect();
    }, 1000);
  };

  ws.onerror = function(err) 
  {
    console.error('Socket encountered error: ', err.message, 'Closing socket');
    ws.close();
  };
}

$("#radioEnable").change(function()
{
    updateRadioFormState();
});

$("#wifiEnable").change(function()
{
    updateWifiFormState();
});

$("#wifiMode").change(function()
{
    updateWifiFormState();
});

$("#wifiSecurityType").change(function()
{
    updateWifiFormState();
});

$("#wifiSave").click(function()
{
    var obj = 
    {
        "type": "saveWifiInfo",
        "enabled": $("#wifiEnable").is(':checked'),
        "mode": parseInt($("#wifiMode").val()),
        "security": parseInt($("#wifiSecurityType").val()),
        "channel": parseInt($("#wifiChannel").val()),
        "ssid": $("#wifiSSID").val(),
        "password": $("#wifiPassword").val()
    };
    
    $("#wifiSuccessAlertRow").hide();
    $("#wifiFailAlertRow").hide();
    
    // Async send request and wait for response.
    ws.send(JSON.stringify(obj));
});

$("#radioSave").click(function()
{
    var obj = 
    {
        "type": "saveRadioInfo",
        "enabled": $("#radioEnable").is(':checked'),
        "host": $("#radioIP").val(),
        "port": parseInt($("#radioPort").val()),
        "username": $("#radioUsername").val(),
        "password": $("#radioPassword").val()
    };
    
    $("#radioSuccessAlertRow").hide();
    $("#radioFailAlertRow").hide();
    
    // Async send request and wait for response.
    ws.send(JSON.stringify(obj));
});

//==========================================================================================
// Disable all form elements on page load. Connect to WebSocket and wait for initial messages.
// These messages will trigger prefilling and reenabling of the form.
//==========================================================================================
$( document ).ready(function() 
{
    $(".wifi-enable-row").hide();
    $("#wifiEnable").prop("disabled", true);
    $("#wifiReset").prop("disabled", true);
    
    $("#wifiSuccessAlertRow").hide();
    $("#wifiFailAlertRow").hide();
    
    $(".radio-enable-row").hide();
    $("#radioEnable").prop("disabled", true);
    $("#radioReset").prop("disabled", true);
    
    $("#radioSuccessAlertRow").hide();
    $("#radioFailAlertRow").hide();
    
    wsConnect();
});