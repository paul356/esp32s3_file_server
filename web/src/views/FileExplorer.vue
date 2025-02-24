<template>
<div class="file_explorer">
  <h1>文件浏览器</h1>
  <table>
    <tr>
      <th class="name_col">文件名</th>
      <th class="size_col">文件大小</th>
      <th class="time_col">修改时间</th>
      <th class="action_col">操作</th>
    </tr>
    <tr v-for="item in this.file_list">
      <td class="name_col">{{ item.name }}</td>
      <td class="size_col">{{ item.size }}</td>
      <td class="time_col">{{ item.mtime }}</td>
      <td class="action_col"><button v-on:click="click_download(item.name)" class="boardless">{{ item.name.endsWith("/") ? "进入" : "下载" }}</button> <button v-on:click="click_delete(item.name)" class="boardless">删除</button></td>
    </tr>
  </table>
  <button v-on:click="create_subdir()" class="boardless">新建目录</button><label>{{ curr_path }}</label><input type="text" id="dir_input" v-on:keyup.enter="create_subdir()" />
</div>
</template>

<script>
import { ref } from 'vue';
import axios from 'axios';

export default {
    setup() {
        const curr_path = ref("/");
        const file_list = ref([]);

        function get_file_list(file_path) {
            axios.get(`/get${file_path}`)
                .then(function (response) {
                    console.log(`get response.data ${response.data}`);
                    file_list.value = response.data;
                })
                .catch(function (error) {
                    file_list.value = [{"name": "oops 1", "size": 100, "mtime": "1970-01-01 00:00:00 +0000 (UTC)"}]
                    window.alert(`can't get ${file_path}, error=${error}`);
                });
        }

        function click_download(file_name) {
            if (file_name.endsWith("/")) {
                curr_path.value = curr_path.value + file_name;
                get_file_list(curr_path.value)
            } else {
                const download_uri = encodeURI('/get' + curr_path.value + file_name);
                axios.get(download_uri)
                    .then(function (response) {
                        console.log(`File ${download_uri} is downloaded`);
                    })
                    .catch(function (error) {
                        window.alert(`can't download ${file_name}, error=${error}`)
                    });
            }
        }

        function click_delete(file_name) {
            const delete_uri = encodeURI('/delete' + curr_path.value + file_name);
            axios.delete(delete_uri)
                .then(function (response) {
                    console.log(`File ${delete_uri} is deleted`);
                    get_file_list(curr_path.value); // optimize by remove entry directly
                })
                .catch(function (error) {
                    window.alert(`can't delete ${file_name}, error=${error}`)
                });
        }

        function create_subdir() {
            const dir_input = document.getElementById("dir_input");
            const dir_name = dir_input.value.trim();
            const toks = dir_name.split("/");
            const filtered = toks.filter((tok) => tok != "")
            const create_uri = encodeURI('/upload' + curr_path.value + filtered.join("/") + "/");
            axios.post(create_uri)
                .then(function (response) {
                    console.log(`Directory ${dir_name} is created`);
                    get_file_list(curr_path.value);
                })
                .catch(function (error) {
                    window.alert(`can't create dir ${dir_name}, error=${error}`)
                });
        }

        get_file_list(curr_path.value);
        return {
            curr_path,
            file_list,
            click_download,
            click_delete,
            create_subdir,
        };
    },
};
</script>

<style scoped>
h1 {
  color: #42b983;
}
table {
    margin-left: auto;
    margin-right: auto;
    border-collapse: collapse;
}
th, tr, td {
    border: 1px solid;
}
td {
    text-align: left;
}
</style>
