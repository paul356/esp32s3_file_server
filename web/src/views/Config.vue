<template>
<div class="config">
  <h1>WiFi设置</h1>
  <div class="wifi_container">
    <label class="config_title">工作模式</label>
    <select id="mode_select">
      <option value="UNKNOWN" v-bind:selected="wifi_config['mode'] == 'UNKNOWN'">--请选择--</option>
      <option value="AP" v-bind:selected="wifi_config['mode'] == 'AP'">WiFi热点</option>
      <option value="STA" v-bind:selected="wifi_config['mode'] == 'STA'">客户端</option>
      <option value="NULL" v-bind:selected="wifi_config['mode'] == 'NULL'">关闭WiFi</option>
    </select>
    <label class="config_title">SSID</label><input type="text" id="ssid_input" v-bind:value="wifi_config['ssid']" />
    <label class="config_title">密码</label><input type="password" id="passwd_input" v-bind:value="wifi_config['passwd']"/>
  </div>
  <br/>
  <button v-on:click="submit_wifi_config()">提交修改</button>
</div>
</template>

<script>
import { ref } from 'vue';
import axios from 'axios';

export default {
    setup() {
        const wifi_config = ref({"mode": "UNKOWN", "ssid": "", "passwd": ""});

        function update_wifi_config() {
            const wifi_uri = "/device/wifi";
            axios.get(wifi_uri)
                .then(function (response) {
                    console.log(`get response ${response.data}`);
                    var config = Object.assign({}, response.data);
                    config['passwd'] = "********";
                    wifi_config.value = config;
                })
                .catch(function (error) {
                    window.alert(`fail to get wifi config, error=${error}`);
                    wifi_config.value = {"mode": "UNKOWN", "ssid": "", "passwd": "********"};
                });
        }

        function submit_wifi_config() {
            const mode_select = document.getElementById("mode_select");
            const ssid_input = document.getElementById("ssid_input");
            const passwd_input = document.getElementById("passwd_input");

            const selected_mode = mode_select.options[mode_select.selectedIndex].value;
            if (selected_mode == "UNKNOWN") {
                window.alert("Please select a mode.");
                return;
            }

            const ssid = ssid_input.value.trim();
            const passwd = passwd_input.value.trim();
            if (selected_mode != "NULL" && ssid == "") {
                window.alert("SSID should not be empty");
                return;
            }

            const wifi_config_json = {
                "mode" : selected_mode,
                "ssid" : ssid,
                "passwd" : passwd
            };

            mode_select.disabled = true;
            ssid_input.disabled = true;
            passwd_input.disabled = true;
            const config_wifi_uri = "/device/wifi";
            axios.put(config_wifi_uri, wifi_config_json)
                .then((response) => {
                    console.log("wifi config is updated");
                    wifi_config.value = {
                        "mode" : selected_mode,
                        "ssid" : ssid,
                        "passwd" : "********"
                    };
                })
                .catch((error) => {
                    window.alert(`can't change wifi config, error=${error}`);
                })
                .finally(() => {
                    mode_select.disabled = false;
                    ssid_input.disabled = false;
                    passwd_input.disabled = false;
                });
        }

        update_wifi_config();
        return {
            wifi_config,
            submit_wifi_config,
        };
    }
};
</script>

<style scoped>
h1 {
  color: #42b983;
}
.wifi_container {
    display: grid;
    grid-template-columns: 70px 120px;
    grid-template-rows: auto auto auto;
    justify-content: center;
    grid-gap: 1px;
}
.config_title {
    background-color: #42b983;
    color: white;
}
</style>
