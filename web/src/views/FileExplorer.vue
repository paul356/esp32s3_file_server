<template>
<div class="file_explorer">
  <br/>
  <div class="path_container">
    <button v-for="path in path_list" v-on:click="goto_directory(path)" class="path_btn borderless">{{ path.length > 1 ? path.substr(path.lastIndexOf("/", path.length - 2) + 1) : path }}</button>
  </div>
  <br/>
  <table>
    <tr>
      <th class="name_col">文件名</th>
      <th class="size_col">大小</th>
      <th class="time_col">修改时间</th>
      <th class="action_col">操作</th>
    </tr>
    <tr v-for="item in file_list">
      <td class="name_col">{{ item.name }}</td>
      <td class="size_col">{{ item.size }}</td>
      <td class="time_col">{{ item.mtime }}</td>
      <td class="action_col">
        <button v-on:click="click_download(item.name, $event)" class="borderless download_btn">{{ item.name.endsWith("/") ? "进入" : "下载" }}</button> <button v-on:click="click_delete(item.name, $event)" class="borderless delete_btn">删除</button>
      </td>
    </tr>
  </table>
  <br/>
  <div class="upload_container">
    <button v-on:click="create_subdir()" class="borderless" id="dir_button">新建目录</button>
    <label>{{ curr_dir }}</label>
    <input type="text" id="dir_input" v-on:keyup.enter="create_subdir()" />
    <button v-on:click="upload_file()" class="borderless" id="file_btn" >上传文件</button>
    <label>{{ curr_dir }}</label>
    <input type="file" id="file_input" />
  </div>
  <a style="display: none;" href="#" download="" id="download_anchor"></a>
</div>
</template>

<script>
import { ref, computed } from 'vue';
import axios from 'axios';

export default {
    setup() {
        const curr_dir_key = "curr_directory";
        const curr_dir = ref(sessionStorage.getItem("curr_directory") || "/");
        function update_curr_dir(new_dir) {
            curr_dir.value = new_dir;
            sessionStorage.setItem(curr_dir_key, new_dir);
        }

        const path_list = computed(() => {
            const paths = curr_dir.value.split("/").filter((tok) => tok != "");
            if (paths.length == 0) {
                return ["/"];
            } else {
                return paths.reduce((accumu, curr) => {
                    accumu.push(accumu.at(-1) + curr + "/");
                    return accumu;
                }, ["/"]);
            }
        });

        const file_list = ref([]);

        function update_file_list(file_path) {
            const get_uri = encodeURI(`/get${file_path}`);
            axios.get(get_uri)
                .then(function (response) {
                    console.log(`get response ${response.data}`);
                    file_list.value = response.data;
                })
                .catch(function (error) {
                    file_list.value = [{"name": "oops 1", "size": 100, "mtime": "1970-01-01 00:00:00 +0000 (UTC)"}]
                    window.alert(`can't get ${file_path}, error=${error}`);
                });
        }

        function goto_directory(directory_path) {
            update_curr_dir(directory_path);
            update_file_list(directory_path);
        }

        function click_download(file_name, event) {
            if (file_name.endsWith("/")) {
                const new_dir = curr_dir.value + file_name;
                update_curr_dir(new_dir);
                update_file_list(new_dir);
            } else {
                event.target.disabled = true;
                const download_uri = encodeURI('/get' + curr_dir.value + file_name);
                const a = document.getElementById('download_anchor');
                a.href = download_uri;
                a.click();
                event.target.disabled = false;
            }
        }

        function click_delete(file_name, event) {
            const delete_uri = encodeURI('/delete' + curr_dir.value + file_name);
            event.target.disabled = true;
            axios.delete(delete_uri)
                .then(function (response) {
                    console.log(`File ${delete_uri} is deleted`);
                    update_file_list(curr_dir.value); // optimize by remove entry directly
                })
                .catch(function (error) {
                    window.alert(`can't delete ${file_name}, error=${error}`)
                })
                .finally(function () {
                    event.target.disabled = false;
                });
        }

        function create_subdir() {
            const dir_input = document.getElementById("dir_input");
            const dir_button = document.getElementById("dir_button");
            const dir_name = dir_input.value.trim();
            const toks = dir_name.split("/");
            const filtered = toks.filter((tok) => tok != "")

            const filtered_name = filtered.join("/");
            if (filtered_name.length == 0) {
                window.alert(`dir "${dir_name}" is not valid.`);
                return;
            }

            const create_uri = encodeURI('/upload' + curr_dir.value + filtered_name + "/");
            dir_input.disabled = true;
            dir_input.disabled = true;
            axios.post(create_uri)
                .then(function (response) {
                    console.log(`Directory ${dir_name} is created`);
                    update_file_list(curr_dir.value);
                })
                .catch(function (error) {
                    window.alert(`can't create dir ${dir_name}, error=${error}`)
                })
                .finally(function () {
                    dir_button.disabled = false;
                    dir_input.value = "";
                    dir_input.disabled = false;
                });
        }

        function upload_file() {
            const file_input = document.getElementById("file_input");
            const file_btn = document.getElementById("file_btn");

            const sel_files = file_input.files;
            if (sel_files.length == 0) {
                window.alert("未选择文件");
                return;
            }

            const file_name = sel_files[0].name;
            if (file_name[file_name.length - 1] == '/') {
                window.alert("不支持上传整个目录");
                return;
            }

            file_input.disabled = true;
            file_btn.disabled = true;

            const upload_url = encodeURI("/upload" + curr_dir.value + file_name);
            axios.post(upload_url, sel_files[0])
                .then((response) => {
                    console.log(`file ${file_name} is uploaded`);
                    update_file_list(curr_dir.value);
                })
                .catch((error) => {
                    window.alert(`can't upload file ${file_name}, error=${error}`);
                })
                .finally(() => {
                    file_input.disabled = false;
                    file_btn.disabled = false;
                    file_input.value = "";
                });
        }

        update_file_list(curr_dir.value);
        return {
            curr_dir,
            path_list,
            file_list,
            goto_directory,
            click_download,
            click_delete,
            create_subdir,
            upload_file,
        };
    },
};
</script>

<style scoped>
label {
  color: #42b983;
}
table {
    margin-left: auto;
    margin-right: auto;
    border-collapse: collapse;
}
th, tr, td {
    border: 1px solid;
    border-color: #dddddd;
}
td {
    text-align: left;
}
.path_container {
    display: flex;
    flex-direction: row;
    gap: 2px;
    justify-content: center;
}
.upload_container {
    display: grid;
    grid-template-columns: 70px auto 200px;
    grid-template-rows: auto auto;
    justify-content: center;
    grid-gap: 2px;
}
.path_btn {
    font-size: 24px;
    background-color: #42b983;
    color: white;
}
.borderless {
    border: 0px;
    background-color: #42b983;
    color: white;
}
.borderless:hover {
    background-color: #215cc1;
}
.delete_btn:hover {
    background-color: #ff0000;
}
</style>
