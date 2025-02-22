<template>
  <div class="file_explorer">
    <h1>Home Page</h1>
    <div v-for="item in this.list">
         {{ item.filename }}
    </div>
  </div>
</template>

<script>
import { ref } from 'vue';

export default {
  setup() {
    var list = ref([{"filename": "first-file"}, {"filename": "second-file"}])
    return {
      list
    }
  },
};
</script>

<style scoped>
h1 {
  color: #42b983;
}
</style>
