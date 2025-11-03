<script>
  let cameraIP = "";
  let streamUrl = "";
  let isConnected = false;

  const connectToStream = () => {
    if (!cameraIP.trim()) return;
    isConnected = true;
    streamUrl = `http://${cameraIP}/stream`;
  };

  const handleKeyPress = (event) => {
    if (event.key === 'Enter') connectToStream();
  };

  const disconnect = () => {
    isConnected = false;
    streamUrl = "";
    cameraIP = "";
  };
</script>

<main>
  <header>
    <h1>📹 ESP32-CAM Viewer</h1>
    <p>Por Pedro Giacoia - TDPII Software</p>
  </header>

  <div class="controls">
    <input
      type="text"
      bind:value={cameraIP}
      on:keypress={handleKeyPress}
      placeholder="IP de la ESP32 (ej: 192.168.1.100)"
      disabled={isConnected}
    />
    
    {#if !isConnected}
      <button on:click={connectToStream} disabled={!cameraIP.trim()}>
        Conectar
      </button>
    {:else}
      <button on:click={disconnect} class="disconnect">
        Desconectar
      </button>
    {/if}
  </div>

  {#if isConnected}
    <div class="video-container">
      <img 
        src={streamUrl} 
        alt="Live Stream" 
        class="video"
        on:error={() => isConnected = false}
      />
      <div class="status">
        ✅ Conectado a: {cameraIP}
      </div>
    </div>
  {:else}
    <div class="welcome">
      <p>Ingresa la IP de tu ESP32-CAM y presiona Enter</p>
    </div>
  {/if}
</main>

<style>
  main {
    padding: 20px;
    max-width: 800px;
    margin: 0 auto;
    font-family: Arial, sans-serif;
    background: linear-gradient(135deg, #0f172a 0%, #1e293b 100%);
    min-height: 100vh;
    color: white;
  }

  header {
    text-align: center;
    margin-bottom: 30px;
  }

  h1 {
    color: #3b82f6;
    margin: 0;
  }

  header p {
    color: #94a3b8;
    margin-top: 5px;
  }

  .controls {
    display: flex;
    gap: 10px;
    margin-bottom: 20px;
  }

  input {
    flex: 1;
    padding: 12px;
    border: 2px solid #475569;
    border-radius: 8px;
    font-size: 16px;
    background: #1e293b;
    color: white;
  }

  input:focus {
    outline: none;
    border-color: #3b82f6;
  }

  input::placeholder {
    color: #64748b;
  }

  button {
    padding: 12px 24px;
    background: #3b82f6;
    color: white;
    border: none;
    border-radius: 8px;
    cursor: pointer;
    font-size: 16px;
  }

  button:hover:not(:disabled) {
    background: #2563eb;
  }

  button:disabled {
    background: #475569;
    cursor: not-allowed;
  }

  .disconnect {
    background: #ef4444;
  }

  .disconnect:hover {
    background: #dc2626;
  }

  .video-container {
    text-align: center;
  }

  .video {
    max-width: 100%;
    max-height: 70vh;
    border: 2px solid #475569;
    border-radius: 8px;
  }

  .status {
    margin-top: 10px;
    color: #22c55e;
    font-weight: bold;
  }

  .welcome {
    text-align: center;
    color: #94a3b8;
    padding: 40px;
    background: rgba(30, 41, 59, 0.5);
    border-radius: 12px;
    border: 1px solid #334155;
  }
</style>