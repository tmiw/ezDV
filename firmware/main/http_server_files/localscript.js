//==========================================================================================
// Form state change
//==========================================================================================
var updateVoiceKeyerState = function()
{
    if ($("#voiceKeyerEnable").is(':checked'))
    {
        $(".vk-enable-row").show(); 
    }
    else
    {
        $(".vk-enable-row").hide();
    }
};

var updateWifiFormState = function() 
{
    if ($("#wifiEnable").is(':checked'))
    {
        $(".wifi-enable-row").show(); 

        // Enable Radio tab.
        $("#radioTab").prop("disabled", false);
    }
    else
    {
        $(".wifi-enable-row").hide();

        // Additionally disable Radio tab since there's
        // no point in setting up anything there if there's
        // no Wi-Fi.
        $("#radioTab").prop("disabled", true);
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

var saveVoiceKeyerSettings = function() {
    var obj = 
    {
        "type": "saveVoiceKeyerInfo",
        "enabled": $("#voiceKeyerEnable").is(':checked'),
        "secondsToWait": parseInt($("#voiceKeyerSecondsToWait").val()),
        "timesToTransmit": parseInt($("#voiceKeyerTimesToTransmit").val())
    };
    
    $("#voiceKeyerSuccessAlertRow").hide();
    $("#voiceKeyerFailAlertRow").hide();
    
    // Async send request and wait for response.
    ws.send(JSON.stringify(obj));
};

//==========================================================================================
// WebSocket handling
//==========================================================================================
var ws = null;
function wsConnect() 
{
  ws = new WebSocket("ws://" + location.hostname + "/ws");
  ws.binaryType = "arraybuffer";
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
          $("wifiSave").show();
          $("#wifiSaveProgress").hide();

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
          $("radioSave").show();
          $("#radioSaveProgress").hide();

          if (json.success)
          {
              $("#radioSuccessAlertRow").show();
          }
          else
          {
              $("#radioFailAlertRow").show();
          }
      }
      else if (json.type == "voiceKeyerInfo")
      {
          $("#voiceKeyerEnable").prop("disabled", false);
          $("#voiceKeyerReset").prop("disabled", false);
          $("#voiceKeyerEnable").prop("checked", json.enabled);

          $("#voiceKeyerTimesToTransmit").val(json.timesToTransmit);
          $("#voiceKeyerSecondsToWait").val(json.secondsToWait);

          updateVoiceKeyerState();
      }
      else if (json.type == "voiceKeyerSaved")
      {
          $("#voiceKeyerSave").show();
          $("#voiceKeyerSaveProgress").hide();

          if (json.success)
          {
              $("#voiceKeyerSuccessAlertRow").show();
          }
          else
          {
              $("#voiceKeyerFailAlertRow").show();
          }
      }
      else if (json.type == "voiceKeyerUploadComplete")
      {
          // TBD: handle errors
          saveVoiceKeyerSettings();
      }
      else if (json.type == "batteryStatus")
      {
          // Update battery percentage and time remaining
          $(".battery-level").height(json.stateOfCharge.toFixed(0) + "%");
          if (json.stateOfChargeChange == 0)
          {
              $(".time-remaining").hide();
          }
          else
          {
              $(".time-remaining").show();
              if (json.stateOfChargeChange < 0)
              {
                  var numHoursRemaining = json.stateOfCharge / -json.stateOfChargeChange;
                  if (numHoursRemaining >= 10)
                  {
                    $(".time-remaining").text("(>10h remaining)");
                  }
                  else if (numHoursRemaining > 1)
                  {
                    $(".time-remaining").text("(" + numHoursRemaining.toFixed(0) + "h remaining)");
                  }
                  else
                  {
                    var numMinutesRemaining = numHoursRemaining * 60;
                    $(".time-remaining").text("(" + numMinutesRemaining.toFixed(0) + " min remaining)");
                  }
              }
              else
              {
                var numHoursRemaining = (100 - json.stateOfCharge) / json.stateOfChargeChange;
                  if (numHoursRemaining >= 10)
                  {
                    $(".time-remaining").text("(>10h to full)");
                  }
                  else if (numHoursRemaining > 1)
                  {
                    $(".time-remaining").text("(" + numHoursRemaining.toFixed(0) + "h to full)");
                  }
                  else
                  {
                    var numMinutesRemaining = numHoursRemaining * 60;
                    $(".time-remaining").text("(" + numMinutesRemaining.toFixed(0) + " min to full)");
                  }
              }
          }
      }
  };

  ws.onclose = function(e) 
  {
    console.log('Socket is closed. Reconnect will be attempted in 1 second.', e.reason);
    setTimeout(function() {
        wsConnect();
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

$("#voiceKeyerEnable").change(function()
{
    updateVoiceKeyerState();
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
    $("wifiSave").hide();
    $("#wifiSaveProgress").show();

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
    $("radioSave").hide();
    $("#radioSaveProgress").show();

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


$("#voiceKeyerSave").click(function()
{
    $("#voiceKeyerSave").hide();
    $("#voiceKeyerSaveProgress").show();

    if ($('#voiceKeyerFile').get(0).files.length === 0) 
    {
        // Skip directly to saving settings if we didn't select
        // a file.
        saveVoiceKeyerSettings();
    }
    else
    {
        var reader = new FileReader();
        reader.onload = function() 
        {
            // read successful, send to server
            var startMessage = {
                type: "uploadVoiceKeyerFile",
                size: reader.result.byteLength
            };
            ws.send(JSON.stringify(startMessage));

            // Send 4K blocks to ezDV so it can better handle
            // them (vs. sending 100K+ at once).
            for (var size = 0; size < reader.result.byteLength; size += 4096)
            {
                ws.send(reader.result.slice(size, size + 4096));
            }
        };
        reader.onerror = function()
        {
            alert("Could not open file for upload!");

            $("#voiceKeyerSave").show();
            $("#voiceKeyerSaveProgress").hide();
        };

        reader.readAsArrayBuffer($('#voiceKeyerFile').get(0).files[0]);
    }

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
    
    $("wifiSave").show();
    $("#wifiSaveProgress").hide();

    $("#wifiSuccessAlertRow").hide();
    $("#wifiFailAlertRow").hide();
    
    $(".radio-enable-row").hide();
    $("#radioEnable").prop("disabled", true);
    $("#radioReset").prop("disabled", true);
    
    $("radioSave").show();
    $("#radioSaveProgress").hide();

    $("#radioSuccessAlertRow").hide();
    $("#radioFailAlertRow").hide();
    
    $(".vk-enable-row").hide();
    $("#voiceKeyerEnable").prop("disabled", true);
    $("#voiceKeyerReset").prop("disabled", true);
    
    $("#voiceKeyerSave").show();
    $("#voiceKeyerSaveProgress").hide();
    
    $("#voiceKeyerSuccessAlertRow").hide();
    $("#voiceKeyerFailAlertRow").hide();

    wsConnect();
});