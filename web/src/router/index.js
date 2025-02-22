import Vue from 'vue';
import VueRouter from 'vue-router';
import FileExplorer from '../views/FileExplorer.vue';
import Config from '../views/Config.vue';

Vue.use(VueRouter);

const routes = [
  {
    path: '/',
    name: 'File Explorer',
    component: FileExplorer
  },
  {
    path: '/config',
    name: 'Config',
    component: Config
  }
];

const router = new VueRouter({
  mode: 'history',
  base: process.env.BASE_URL,
  routes
});

export default router;
