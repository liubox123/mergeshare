<template>
  <slot :spectrumData="spectrumData"></slot>
</template>

<script setup>
import { ref, onMounted, onBeforeUnmount } from 'vue';

const props = defineProps({
  wsUrl: { type: String, required: true },
});

const spectrumData = ref([]);
let ws = null;

onMounted(() => {
  ws = new WebSocket(props.wsUrl);
  ws.onmessage = async (event) => {
    let arrayBuffer;
    if (typeof event.data === 'string') {
      // 可选：解析首包参数
      return;
    }
    if (event.data instanceof Blob) {
      arrayBuffer = await event.data.arrayBuffer();
    } else if (event.data instanceof ArrayBuffer) {
      arrayBuffer = event.data;
    }
    if (arrayBuffer) {
      const arr = new Float32Array(arrayBuffer);
      spectrumData.value = Array.from(arr);
    }
  };
});

onBeforeUnmount(() => {
  if (ws) ws.close();
});
</script>
