<template>
  <SpectrumDataProvider ws-url="ws://localhost:9003/ws">
    <template #default="{ spectrumData }">
      <NativeWebGLSpectrumChart
        :spectrum-data="spectrumData"
        :freq-min="0"
        :freq-max="8e9"
        :width="800"
        :height="400"
      />
      <NativeWebGLWaterfallAllGpu
        :waterfall-data="spectrumData"
        :freq-min="0"
        :freq-max="8e9"
        :max-history="100"
        :width="800"
        :height="400"
      />
    </template>
  </SpectrumDataProvider>
</template>

<script>
import SpectrumDataProvider from './components/SpectrumDataProvider.vue';
import NativeWebGLSpectrumChart from './components/NativeWebGLSpectrumChart.vue';
import NativeWebGLWaterfallAllGpu from './components/NativeWebGLWaterfallAllGpu.vue';

export default {
  name: 'App',
  components: {
    SpectrumDataProvider,
    NativeWebGLSpectrumChart,
    NativeWebGLWaterfallAllGpu,
  },
};
</script>

