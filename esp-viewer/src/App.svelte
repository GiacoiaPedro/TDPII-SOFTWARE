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

<main class="app-container">
  <div class="content">
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
  </div>
</main>

<style>
  .app-container {
    min-height: 100vh;
    background: 
      linear-gradient(rgba(15, 23, 42, 0.85), rgba(30, 41, 59, 0.90)),
      url('/background.jpg') center/cover;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 20px;
  }

  .content {
    background: rgba(30, 41, 59, 0.8);
    backdrop-filter: blur(10px);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 16px;
    padding: 30px;
    max-width: 800px;
    width: 100%;
    box-shadow: 0 20px 40px rgba(0, 0, 0, 0.3);
  }

  header {
    text-align: center;
    margin-bottom: 30px;
  }

  h1 {
    color: #3b82f6;
    margin: 0;
    font-size: 2rem;
  }

  header p {
    color: #94a3b8;
    margin-top: 8px;
    font-size: 1rem;
  }

  .controls {
    display: flex;
    gap: 12px;
    margin-bottom: 25px;
  }

  input {
    flex: 1;
    padding: 14px;
    border: 2px solid #475569;
    border-radius: 10px;
    font-size: 16px;
    background: rgba(15, 23, 42, 0.7);
    color: white;
    transition: all 0.3s ease;
  }

  input:focus {
    outline: none;
    border-color: #3b82f6;
    box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.2);
  }

  input::placeholder {
    color: #64748b;
  }

  button {
    padding: 14px 28px;
    background: #3b82f6;
    color: white;
    border: none;
    border-radius: 10px;
    cursor: pointer;
    font-size: 16px;
    font-weight: 600;
    transition: all 0.3s ease;
  }

  button:hover:not(:disabled) {
    background: #2563eb;
    transform: translateY(-2px);
  }

  button:disabled {
    background: #475569;
    cursor: not-allowed;
    transform: none;
  }

  .disconnect {
    background: #ef4444;
  }

  .disconnect:hover {
    background: #dc2626;
  }

  .video-container {
    text-align: center;
    background: rgba(15, 23, 42, 0.6);
    padding: 20px;
    border-radius: 12px;
    border: 1px solid rgba(255, 255, 255, 0.1);
  }

  .video {
    max-width: 100%;
    max-height: 60vh;
    border-radius: 8px;
    border: 2px solid #475569;
  }

  .status {
    margin-top: 15px;
    color: #22c55e;
    font-weight: bold;
    font-size: 1.1rem;
  }

  .welcome {
    text-align: center;
    color: #94a3b8;
    padding: 50px 30px;
    background: rgba(15, 23, 42, 0.6);
    border-radius: 12px;
    border: 1px solid rgba(255, 255, 255, 0.1);
    font-size: 1.1rem;
  }
</style>