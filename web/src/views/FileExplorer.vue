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
      <td class="action_col"><a :href="encodeURI('/get/' + item.name)">下载</a> <a :href="encodeURI('/delete/' + item.name)">删除</a></td>
    </tr>
  </table>
</div>
</template>

<script>
import { ref } from 'vue';
import axios from 'axios';

export default {
    setup() {
        const file_list = ref([]);
        function get_file_list(file_path) {
            axios.get(`/get${file_path}`)
                .then(function (response) {
                    console.log(`get response.data ${response.data}`)
                    file_list.value = response.data
                })
                .catch(function (error) {
                    file_list.value = [{"name": "oops 1", "size": 0, "mtime": "1970-01-01 00:00:00 +0000 (UTC)"},
                                       {"name": "oops 2", "size": 1, "mtime": "1971-01-01 00:00:00 +0000 (UTC)"}]
                    console.log(`error get ${file_path}, error=${error}`)
                });
        }
        get_file_list("/");

        return {
            file_list
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
