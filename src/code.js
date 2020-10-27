// save some bytes
const gel = (e) => document.getElementById(e);

const wifi_div = gel("wifi");
const connect_div = gel("connect");
const connect_manual_div = gel("connect_manual");
const connect_wait_div = gel("connect-wait");
const connect_details_div = gel("connect-details");

function docReady(fn) {
  // see if DOM is already available
  if (
    document.readyState === "complete" ||
    document.readyState === "interactive"
  ) {
    // call on next available tick
    setTimeout(fn, 1);
  } else {
    document.addEventListener("DOMContentLoaded", fn);
  }
}

var selectedSSID = "";
var refreshAPInterval = null;
var checkStatusInterval = null;
var checkMqttStatusInterval = null;

function stopCheckStatusInterval() {
  if (checkStatusInterval != null) {
    clearInterval(checkStatusInterval);
    checkStatusInterval = null;
  }
}

function stopRefreshAPInterval() {
  if (refreshAPInterval != null) {
    clearInterval(refreshAPInterval);
    refreshAPInterval = null;
  }
}

function stopCheckMqttStatusInterval() {
  if (checkMqttStatusInterval != null) {
    clearInterval(checkMqttStatusInterval);
    checkMqttStatusInterval = null;
  }
}

function startCheckStatusInterval() {
  checkStatusInterval = setInterval(checkStatus, 950);
}

function startRefreshAPInterval() {
  refreshAPInterval = setInterval(refreshAP, 3800);
}

function startCheckMqttStatusInterval() {
  checkMqttStatusInterval = setInterval(checkMqttStatus, 950);
}


docReady(async function () {

  gel("mqtt-details-wrap").style.display = "none";
  gel("mqtt-connect-wrap").style.display = "block";
  gel("mqtt-connecting").style.display = "none";
  gel("mqtt-connect-fail").style.display = "none";
  gel("mqtt-details").style.display = "none";

  gel("wifi-status").addEventListener(
    "click",
    () => {
      wifi_div.style.display = "none";
      gel("connect-details").style.display = "block";
    },
    false
  );

  gel("mqtt-status").addEventListener(
    "click",
    () => {
      wifi_div.style.display = "none";
      gel("mqtt-details").style.display = "block";
    },
    false
  );

  gel("manual_add").addEventListener(
    "click",
    (e) => {
      selectedSSID = e.target.innerText;

      gel("ssid-pwd").textContent = selectedSSID;
      wifi_div.style.display = "none";
      connect_manual_div.style.display = "block";
      connect_div.style.display = "none";

      gel("connect-success").display = "none";
      gel("connect-fail").display = "none";
    },
    false
  );

  gel("wifi-list").addEventListener(
    "click",
    (e) => {
      selectedSSID = e.target.innerText;
      gel("ssid-pwd").textContent = selectedSSID;
      connect_div.style.display = "block";
      wifi_div.style.display = "none";
      // init_cancel();
    },
    false
  );

  function cancel() {
    selectedSSID = "";
    connect_div.style.display = "none";
    connect_manual_div.style.display = "none";
    wifi_div.style.display = "block";
  }

  function MqttCancel() {
    gel("mqtt-details").style.display = "none";
    wifi_div.style.display = "block";
  }


  gel("cancel").addEventListener("click", cancel, false);

  gel("manual_cancel").addEventListener("click", cancel, false);

  gel("join").addEventListener("click", performConnect, false);

  gel("manual_join").addEventListener(
    "click",
    (e) => {
      performConnect("manual");
    },
    false
  );

  gel("ok-details").addEventListener(
    "click",
    () => {
      connect_details_div.style.display = "none";
      wifi_div.style.display = "block";
    },
    false
  );

  gel("ok-credits").addEventListener(
    "click",
    () => {
      gel("credits").style.display = "none";
      gel("app").style.display = "block";
    },
    false
  );

  gel("acredits").addEventListener(
    "click",
    () => {
      event.preventDefault();
      gel("app").style.display = "none";
      gel("credits").style.display = "block";
    },
    false
  );

  gel("ok-connect").addEventListener(
    "click",
    () => {
      connect_wait_div.style.display = "none";
      wifi_div.style.display = "block";
    },
    false
  );

  gel("disconnect").addEventListener(
    "click",
    () => {
      gel("diag-disconnect").style.display = "block";
      gel("connect-details-wrap").classList.add("blur");
    },
    false
  );

  gel("no-disconnect").addEventListener(
    "click",
    () => {
      gel("diag-disconnect").style.display = "none";
      gel("connect-details-wrap").classList.remove("blur");
    },
    false
  );

  gel("yes-disconnect").addEventListener("click", async () => {
    stopCheckStatusInterval();
    selectedSSID = "";

    document.getElementById("diag-disconnect").style.display = "none";
    gel("connect-details-wrap").classList.remove("blur");

    await fetch("connect.json", {
      method: "DELETE",
      headers: {
        "Content-Type": "application/json",
      },
      body: { timestamp: Date.now() },
    });

    startCheckStatusInterval();

    connect_details_div.style.display = "none";
    wifi_div.style.display = "block";
  });

  gel("mqtt-cancel").addEventListener("click", MqttCancel, false);
  gel("mqtt-join").addEventListener("click", performMqttConnect, false);

  gel("mqtt-fail-ok").addEventListener(
    "click",
    () => {
      gel("mqtt-connect-fail").style.display = "none";
      gel("mqtt-connect").style.display = "block";
    },
    false
  );

  gel("mqtt-disconnect").addEventListener(
    "click",
    () => {
      gel("diag-mqtt-disconnect").style.display = "block";
      gel("mqtt-details-wrap").classList.add("blur");
    },
    false
  );

  gel("mqtt-no-disconnect").addEventListener(
    "click",
    () => {
      gel("diag-mqtt-disconnect").style.display = "none";
      gel("mqtt-details-wrap").classList.remove("blur");
    },
    false
  );

  gel("mqtt-yes-disconnect").addEventListener("click", async () => {
    gel("diag-mqtt-disconnect").style.display = "none";
    gel("mqtt-details-wrap").classList.remove("blur");

    await fetch("connect.json", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "X-Custom-mqtt-uri": "DISCONNECT",
      },
      body: { timestamp: Date.now() },
    });

    gel("mqtt-details-wrap").style.display = "none";
    gel("mqtt-connect-wrap").style.display = "block";
  });

  gel("mqtt-ok-details").addEventListener(
    "click",
    () => {
      gel("mqtt-details").style.display = "none";
      wifi_div.style.display = "block";
    },
    false
  );




  //first time the page loads: attempt get the connection status and start the wifi scan
  await refreshAP();
  startCheckStatusInterval();
  startRefreshAPInterval();
  startCheckMqttStatusInterval();
});

async function performConnect(conntype) {
  //stop the status refresh. This prevents a race condition where a status
  //request would be refreshed with wrong ip info from a previous connection
  //and the request would automatically shows as succesful.
  stopCheckStatusInterval();

  //stop refreshing wifi list
  stopRefreshAPInterval();

  var pwd;
  if (conntype == "manual") {
    //Grab the manual SSID and PWD
    selectedSSID = gel("manual_ssid").value;
    pwd = gel("manual_pwd").value;
  } else {
    pwd = gel("pwd").value;
  }

  // bypass bug in ESP http parsing (header value cannot be empty)
  if (pwd == "")
    pwd = "__EMPTY__";

  //reset connection
  gel("loading").style.display = "block";
  gel("connect-success").style.display = "none";
  gel("connect-fail").style.display = "none";

  gel("ok-connect").disabled = true;
  gel("ssid-wait").textContent = selectedSSID;
  connect_div.style.display = "none";
  connect_manual_div.style.display = "none";
  connect_wait_div.style.display = "block";

  let response = await fetch("connect.json", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "X-Custom-ssid": selectedSSID,
      "X-Custom-pwd": pwd,
    },
    body: { timestamp: Date.now() },
  });
  if (!response.ok) {
      // this shouldn't happen. Exit connect wait screen
      console.info("PerformConnect failed!");
      connect_wait_div.style.display = "none";
      wifi_div.style.display = "block";
  }

  //now we can re-set the intervals regardless of result
  startCheckStatusInterval();
  startRefreshAPInterval();
}


async function performMqttConnect() {

  var mqtt_uri, mqtt_username, mqtt_pwd;
  mqtt_uri = gel("mqtt_uri").value;
  mqtt_username = gel("mqtt_username").value;
  mqtt_pwd = gel("mqtt_pwd").value;

  if (mqtt_uri == "")
    return;
  // bypass bug in ESP http parsing (header value cannot be empty)
  if (mqtt_username == "")
    mqtt_username = "__EMPTY__";
  if (mqtt_pwd == "")
    mqtt_pwd = "__EMPTY__";

  document.querySelector("#mqtt-connected-to div div span").textContent =
    mqtt_uri;

  document.querySelector(
    "#mqtt-details-wrap h1"
  ).textContent = mqtt_uri;

  gel("mqtt-connect").style.display = "none";
  gel("mqtt-connecting").style.display = "block";
  gel("mqtt-connect-fail").style.display = "none";


  gel("mqtt-ok-details").disabled = true;

  let response = await fetch("connect.json", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "X-Custom-mqtt-uri": mqtt_uri,
      "X-Custom-mqtt-username": mqtt_username,
      "X-Custom-mqtt-pwd": mqtt_pwd,
    },
    body: { timestamp: Date.now() },
  });
  if (!response.ok) {
      // this shouldn't happen. Exit connect wait screen
      console.info("PerformMqttConnect failed!");
      gel("mqtt-connect").style.display = "block";
      gel("mqtt-connecting").style.display = "none";
  } else {
    startCheckMqttStatusInterval();
  }
}


function rssiToIcon(rssi) {
  if (rssi >= -60) {
    return "w0";
  } else if (rssi >= -67) {
    return "w1";
  } else if (rssi >= -75) {
    return "w2";
  } else {
    return "w3";
  }
}

async function refreshAP(url = "ap.json") {
  try {
    var res = await fetch(url);
    var access_points = await res.json();
    if (access_points.length > 0) {
      //sort by signal strength
      access_points.sort((a, b) => {
        var x = a["rssi"];
        var y = b["rssi"];
        return x < y ? 1 : x > y ? -1 : 0;
      });
      refreshAPHTML(access_points);
    }
  } catch (e) {
    console.info("Access points returned empty from /ap.json!");
  }
}

function refreshAPHTML(data) {
  var h = "";
  data.forEach(function (e, idx, array) {
    let ap_class = idx === array.length - 1 ? "" : " brdb";
    let rssicon = rssiToIcon(e.rssi);
    let auth = e.auth == 0 ? "" : "pw";
    h += `<div class="ape${ap_class}"><div class="${rssicon}"><div class="${auth}">${e.ssid}</div></div></div>\n`;
  });

  gel("wifi-list").innerHTML = h;
}

async function checkStatus(url = "status.json") {
  try {
    var response = await fetch(url);
    var data = await response.json();
    if (data && data.hasOwnProperty("ssid") && data["ssid"] != "") {
      if (data["ssid"] === selectedSSID) {
        // Attempting connection
        switch (data["urc"]) {
          case 0:
            console.info("Got connection!");
            document.querySelector(
              "#connected-to div div div span"
            ).textContent = data["ssid"];
            document.querySelector("#connect-details h1").textContent =
              data["ssid"];
            gel("ip").textContent = data["ip"];
            gel("netmask").textContent = data["netmask"];
            gel("gw").textContent = data["gw"];
            gel("wifi-status").style.display = "block";

            //unlock the wait screen if needed
            gel("ok-connect").disabled = false;

            //update wait screen
            gel("loading").style.display = "none";
            gel("connect-success").style.display = "block";
            gel("connect-fail").style.display = "none";
            break;
          case 1:
            console.info("Connection attempt failed!");
            document.querySelector(
              "#connected-to div div div span"
            ).textContent = data["ssid"];
            document.querySelector("#connect-details h1").textContent =
              data["ssid"];
            gel("ip").textContent = "0.0.0.0";
            gel("netmask").textContent = "0.0.0.0";
            gel("gw").textContent = "0.0.0.0";

            //don't show any connection
            gel("wifi-status").display = "none";

            //unlock the wait screen
            gel("ok-connect").disabled = false;

            //update wait screen
            gel("loading").style.display = "none";
            gel("connect-fail").style.display = "block";
            gel("connect-success").style.display = "none";
            break;
          case 2: // user disconnnect
          case 3: // lost connection
            gel("mqtt-status").style.display = "none";
            break;

        }
      } else if (data.hasOwnProperty("urc") && data["urc"] === 0) {
        console.info("Connection established");
        //ESP is already connected to a wifi server without having the user do anything
        if (
          gel("wifi-status").style.display == "" ||
          gel("wifi-status").style.display == "none"
        ) {
          document.querySelector("#connected-to div div div span").textContent =
            data["ssid"];
          document.querySelector("#connect-details h1").textContent =
            data["ssid"];
          gel("ip").textContent = data["ip"];
          gel("netmask").textContent = data["netmask"];
          gel("gw").textContent = data["gw"];
          gel("wifi-status").style.display = "block";
          gel("mqtt-status").style.display = "block";
        }
      } else if (data.hasOwnProperty("urc") && ((data["urc"] === 2) || (data["urc"] === 3))) {
        console.log("Wifi in disconnected state");
        if (gel("wifi-status").style.display == "block") {
          gel("wifi-status").style.display = "none";
        }
        if (gel("mqtt-status").style.display == "block") {
          gel("mqtt-status").style.display = "none";
        }
      }
    }
  } catch (e) {
    console.info("Was not able to fetch /status.json");
  }
}

async function checkMqttStatus(url = "mqtt_status.json") {
  try {
    var response = await fetch(url);
    var data = await response.json();
    if (data && data.hasOwnProperty("uri")) {
      if (data["uri"] === "") {
      } else {
        // Attempting connection
        switch (data["urc"]) {
          case 0:
            console.info("MQTT not yet configured");
            document.querySelector(
              "#mqtt-connected-status div"
            ).textContent = " (Not configured)";
            document.querySelector(
              "#mqtt-details-wrap h2"
            ).textContent = "Not configured";
            document.querySelector(
              "#mqtt-details-wrap h2"
            ).textContent = "";
          case 1:
            console.info("Got MQTT connection!");
            document.querySelector(
              "#mqtt-connected-to div div span"
            ).textContent = data["uri"];
            document.querySelector(
              "#mqtt-connected-status"
            ).textContent = " (Connected)";
            document.querySelector(
              "#mqtt-details-wrap header h1"
            ).textContent = data["uri"];            
            document.querySelector(
              "#mqtt-details-wrap h2"
            ).textContent = "Connected";

            //update wait screen
            gel("mqtt-connect-wrap").style.display = "none";
            gel("mqtt-details-wrap").style.display = "block";
            //gel("mqtt-connect-fail").style.display = "none";
            gel("mqtt-ok-details").disabled = false;
            gel("mqtt-disconnect").disabled = false;
            break;
          case 2:
            console.info("User disconnect");
            document.querySelector(
              "#mqtt-connected-to div div span"
            ).textContent = data["uri"];
            document.querySelector(
              "#mqtt-connected-status"
            ).textContent = " (Disconnected)";
            document.querySelector(
              "#mqtt-details-wrap h2"
            ).textContent = "User disconnect";
            gel("mqtt-connect-wrap").style.display = "block";
            gel("mqtt-connect").style.display = "block";
            gel("mqtt-connecting").style.display = "none";
            gel("mqtt-connect-fail").style.display = "none";
            gel("mqtt-details-wrap").style.display = "none";
            //gel("mqtt-ok-details").disabled = false;
            //gel("mqtt-disconnect").disabled = true;

            stopCheckMqttStatusInterval();

            break;
          case 3: // attempt failed
            document.querySelector(
              "#mqtt-connect-fail h2"
            ).textContent = data["error"];
            // fall through
          case 4: // conn lost
            console.info("Disconnected");
            document.querySelector(
              "#mqtt-connected-to div div span"
            ).textContent = data["uri"];
            document.querySelector(
              "#mqtt-connected-status"
            ).textContent = " (Disconnected)";
            document.querySelector(
              "#mqtt-details-wrap h2"
            ).textContent = "Disconnected";

            //update wait screen
            gel("mqtt-connecting").style.display = "none";
            gel("mqtt-connect-fail").style.display = "block";
            gel("mqtt-fail-ok").disabled = false;
            //gel("mqtt-disconnect").disabled = true;
            gel("mqtt-details-wrap").style.display = "none";

            stopCheckMqttStatusInterval();

            break;
        }
      }
    }
  } catch (e) {
    console.info("Was not able to fetch /mqtt_status.json");
  }
}

