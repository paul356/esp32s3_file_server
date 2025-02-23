<template>
  <div class="file_explorer">
    <h1>Home Page</h1>
    <div v-for="item in this.file_list">
         {{ item.name }}
    </div>
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
                    file_list.value = [{"name": "oops"}]
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
</style>
