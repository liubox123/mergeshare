<template>
  <slot :spectrumData="spectrumData"></slot>
</template>

<script setup>
import {
  shallowRef, onMounted, onBeforeUnmount,
} from 'vue';

const props = defineProps({
  wsUrl: { type: String, required: true },
});

const spectrumData = shallowRef(new Float32Array(0));
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
      // 直接创建新的 Float32Array 并更新引用
      spectrumData.value = new Float32Array(arrayBuffer);
    }
  };
});

onBeforeUnmount(() => {
  if (ws) ws.close();
});
</script>
