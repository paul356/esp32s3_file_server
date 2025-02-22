<template>
  <div id="app">
    <nav>
      <router-link to="/">File Explorer</router-link> |
      <router-link to="/config">Config</router-link>
    </nav>
    <router-view/>
  </div>
</template>

<script>
export default {
  name: 'App'
};
</script>

<style>
#app {
  font-family: Avenir, Helvetica, Arial, sans-serif;
  -webkit-font-smoothing: antialiased;
  -moz-osx-font-smoothing: grayscale;
  text-align: center;
  color: #2c3e50;
  margin-top: 60px;
}
nav a {
  margin: 0 15px;
  color: #42b983;
  text-decoration: none;
}
nav a.router-link-exact-active {
  font-weight: bold;
}
</style>
