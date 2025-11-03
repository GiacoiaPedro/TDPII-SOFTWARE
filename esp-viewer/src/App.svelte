<script>
  import { onMount } from 'svelte';

  let espIp = '192.168.1.100'; // 🔹 poné acá la IP real de tu ESP32-CAM
  let streamUrl = `http://${espIp}:81/stream`;
  let dataUrl = `http://${espIp}/data`;

  let data = { mensaje: 'Esperando datos...' };

  onMount(() => {
    const timer = setInterval(async () => {
      try {
        const res = await fetch(dataUrl);
        data = await res.json();
      } catch (err) {
        data = { error: 'Sin conexión con la ESP' };
      }
    }, 1000);

    return () => clearInterval(timer);
  });
</script>

<style>
  main {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 1rem;
    padding: 1rem;
    background: #111;
    color: #eee;
    height: 100vh;
    font-family: system-ui;
  }
  img {
    border-radius: 8px;
    max-width: 90%;
    box-shadow: 0 0 12px #000;
  }
  pre {
    background: #222;
    padding: 1rem;
    border-radius: 6px;
    text-align: left;
  }
</style>

<main>
  <h1>ESP32 Stream + Datos</h1>
  <img src={streamUrl} alt="Stream ESP32" />
  <pre>{JSON.stringify(data, null, 2)}</pre>
</main>
