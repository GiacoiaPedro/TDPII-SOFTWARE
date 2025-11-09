<script>
  import { onMount } from 'svelte';

  const base_url = "http://192.168.1.84";
  let figurasPosibles = [
      { id: 0, nombre: "Cuadrado", icon: "⬜" },
      { id: 1, nombre: "Círculo", icon: "⚪" },
      { id: 2, nombre: "L", icon: "🅻" },
      { id: 3, nombre: "Estrella", icon: "⭐" },
      { id: 4, nombre: "C", icon: "🅒" }
      ];
  let detectionData = { figura: "Ninguna", confianza: 0 };
  let selectedFigureId = 0;
  let figurasCalibradas = [];
  let lastUpdate = null;
  let isConnected = false;
  let isCalibrating = false;
  let pollingActive = false;
  let notification = { text: "", type: "" };

  let fetchInProgress = false;
  let requestCount = 0;
  let lastRequestTime = null;

  // --- Notificación visual ---
  function showNotification(text, type) {
    notification = { text, type };
    setTimeout(() => (notification = { text: "", type: "" }), 4000);
  }

  // --- Estado ---
  async function fetchStatus() {
    try {
      const res = await fetch(`${base_url}/status`);
      if (res.ok) {
        const data = await res.json();
        figurasCalibradas = data.calibradas || [];
        isConnected = true;
      } else {
        isConnected = false;
      }
    } catch {
      isConnected = false;
    }
  }

  // --- Detección CON SETTIMEOUT RECURSIVO ---
  async function fetchDetection() {
    if (!pollingActive) {
      console.log('⏹️ Polling desactivado, deteniendo...');
      return;
    }
    
    // 🔥 ELIMINAR LA VERIFICACIÓN DE fetchInProgress - dejar que se encolen
    requestCount++;
    lastRequestTime = new Date();
    
    try {
      const startTime = Date.now();
      const res = await fetch(`${base_url}/detect?_t=${Date.now()}`);
      
      if (!res.ok) {
        throw new Error(`HTTP ${res.status}`);
      }
      
      const data = await res.json();
      const endTime = Date.now();
      
      detectionData = data;
      lastUpdate = new Date();
      isConnected = true;
      
      console.log(`✅ [${requestCount}] ${data.figura} (${(data.confianza * 100).toFixed(1)}%) en ${endTime - startTime}ms`);
      
    } catch (error) {
      isConnected = false;
      detectionData = { figura: "Ninguna", confianza: 0 };
      console.error(`💥 [${requestCount}] Error:`, error.message);
    }
    
    // 🔥 PROGRAMAR SIGUIENTE REQUEST 500ms DESPUÉS DEL INICIO DE ESTA
    // Esto asegura un intervalo constante de ~500ms entre inicios
    if (pollingActive) {
      setTimeout(fetchDetection, 500);
    }
  }

  // --- Polling MEJORADO ---
  function startPolling() {
    console.log('🔄 START POLLING');
    pollingActive = true;
    requestCount = 0; // Reset counter
    
    // Iniciar detección inmediatamente
    fetchDetection();
    
    // Status cada 2 segundos
    const pollStatus = () => {
      if (pollingActive) {
        fetchStatus();
        setTimeout(pollStatus, 2000);
      }
    };
    pollStatus();
    
    console.log('📡 Polling recursivo iniciado - 500ms entre requests');
  }
  
  function stopPolling() {
    console.log('⏹️ STOP POLLING');
    pollingActive = false;
  }

  async function calibrarFigura() {
    isCalibrating = true;
    stopPolling();
    
    const imgElement = document.querySelector('.video-container img');
    if (imgElement) imgElement.src = '';

    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 8000);

    try {
      const res = await fetch(`${base_url}/calibrate`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ figure: selectedFigureId }),
        signal: controller.signal,
      });

      clearTimeout(timeoutId);
      const text = await res.text();

      if (res.ok) {
        JSON.parse(text);
        const figName = figurasPosibles[selectedFigureId].nombre;
        if (!figurasCalibradas.includes(figName))
          figurasCalibradas = [...figurasCalibradas, figName];

        showNotification(`✅ ${figName} calibrado correctamente`, "success");
        await fetchStatus();
      } else {
        showNotification(`❌ Error del servidor (${res.status})`, "error");
      }
    } catch (e) {
      if (e.name === "AbortError")
        showNotification("❌ Timeout: sin respuesta de la ESP32", "error");
      else showNotification(`❌ Error: ${e.message}`, "error");
    } finally {
      if (imgElement) imgElement.src = `${base_url}/processed_stream`;
      isCalibrating = false;
      startPolling();
    }
  }

  onMount(() => {
    console.log('🚀 Componente montado');
    startPolling();
  });
</script>

<main class="container">
  <header>
    <h1>Calibración de figuras</h1>
    <div class="connection-bar {isConnected ? 'connected' : ''}">
      <span>{isConnected ? "🟢 Conectado" : "🔴 Desconectado"}</span>
      <span>
        Última actualización:
        {#if lastUpdate}{lastUpdate.toLocaleTimeString()}{:else}(sin datos){/if}
      </span>
    </div>
  </header>

  <!-- PANEL DE DIAGNÓSTICO -->
  <div class="debug-panel">
    <div><strong>Requests:</strong> {requestCount}</div>
    <div><strong>Última:</strong> {detectionData.figura} ({(detectionData.confianza * 100).toFixed(1)}%)</div>
    <div><strong>Polling:</strong> {pollingActive ? '✅ ACTIVO' : '❌ INACTIVO'}</div>
  </div>

  <div class="grid-layout">
    <div class="panel">
      <h2>Detección actual</h2>
      <div class="video-container">
        {#if isConnected}
          <img src={`${base_url}/processed_stream`} alt="Video stream" />
        {:else}
          <div class="placeholder">Conectando con la cámara...</div>
        {/if}
      </div>

      <div class="detection-result">
        <div class="figure-info">
          <span class="figure-name">{detectionData.figura}</span>
          <span class="confidence-value">{(detectionData.confianza * 100).toFixed(1)}%</span>
        </div>
        
        <div class="confidence-meter">
          <div class="progress-bar-bg">
            <div 
              class="progress-bar-fill {detectionData.figura !== 'Ninguna' ? 'active' : ''}"
              style="width: {detectionData.confianza * 100}%"
            ></div>
          </div>
          <div class="confidence-label">
            <span>Confianza</span>
            <span>{(detectionData.confianza * 100).toFixed(1)}%</span>
          </div>
        </div>
      </div>

      <div class="stream-info">
        <span>IP: 192.168.1.84</span>
        <span>Requests: {requestCount} | Intervalo: 500ms</span>
      </div>
    </div>

    <div class="panel controls-column">
      <h2>Calibrar figura</h2>
      <div class="instructions">
        Seleccioná una figura y asegurate que la cámara la vea claramente.
      </div>
      <div class="calibration-form">
        <select bind:value={selectedFigureId}>
          {#each figurasPosibles as f}
            <option value={f.id}>{f.icon} {f.nombre}</option>
          {/each}
        </select>

        <button
          class="calibrate-btn {isCalibrating ? 'loading' : ''}"
          on:click={calibrarFigura}
          disabled={isCalibrating || !isConnected}
        >
          {isCalibrating ? "Calibrando..." : "Calibrar"}
        </button>
      </div>

      <h3>Figuras calibradas</h3>
      <div class="tags">
        {#each figurasPosibles as f}
          <span class="tag {figurasCalibradas.includes(f.nombre) ? 'active' : ''}">
            {f.icon} {f.nombre}
          </span>
        {/each}
      </div>
    </div>
  </div>

  {#if notification.text}
    <div class="notification {notification.type}">{notification.text}</div>
  {/if}
</main>

<style>
  /* (Todos los estilos anteriores se mantienen igual) */
  :global(body) {
    margin: 0;
    font-family: 'Segoe UI', system-ui, sans-serif;
    background: #1a1a2e;
    color: #e0e0e0;
  }

  .container {
    max-width: 1200px;
    margin: 0 auto;
    padding: 20px;
    position: relative;
  }

  header {
    margin-bottom: 30px;
    border-bottom: 2px solid #2a2a40;
    padding-bottom: 20px;
  }

  h1 {
    margin: 0 0 15px 0;
    color: #fff;
  }

  .connection-bar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 10px 15px;
    background: #ef444433;
    border-radius: 8px;
    border-left: 5px solid #ef4444;
    transition: all 0.3s ease;
  }

  .connection-bar.connected {
    background: #22c55e22;
    border-left-color: #22c55e;
  }

  .debug-panel {
    background: #2a2a40;
    padding: 12px 15px;
    border-radius: 8px;
    margin-bottom: 20px;
    font-family: monospace;
    font-size: 0.8rem;
    color: #facc15;
    border-left: 4px solid #facc15;
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 8px;
  }

  .debug-panel div {
    margin: 2px 0;
  }

  .grid-layout {
    display: grid;
    grid-template-columns: 1.5fr 1fr;
    gap: 25px;
  }

  @media (max-width: 768px) {
    .grid-layout { grid-template-columns: 1fr; }
    .debug-panel { grid-template-columns: 1fr; }
  }

  .panel {
    background: #16213e;
    padding: 20px;
    border-radius: 12px;
    box-shadow: 0 4px 6px rgba(0,0,0,0.2);
    border: 1px solid #2a2a40;
  }

  h2 {
    margin-top: 0;
    color: #4cc9f0;
    border-bottom: 1px solid #2a2a40;
    padding-bottom: 10px;
  }

  /* Video Stream Styles */
  .video-container {
    width: 100%;
    aspect-ratio: 4/3;
    background: #000;
    border-radius: 8px;
    overflow: hidden;
    display: flex;
    align-items: center;
    justify-content: center;
  }

  .video-container img {
    width: 100%;
    height: 100%;
    object-fit: contain;
  }

  .placeholder {
    color: #666;
    text-align: center;
  }

  /* Detection Result Styles */
  .detection-result {
    margin: 20px 0;
    padding: 15px;
    background: #0f3460;
    border-radius: 8px;
  }

  .figure-info {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 10px;
  }

  .figure-name {
    font-size: 1.2rem;
    font-weight: bold;
    color: #4cc9f0;
  }

  .confidence-value {
    font-size: 1.1rem;
    color: #fff;
  }

  .confidence-meter {
    margin-top: 10px;
  }

  .progress-bar-bg {
    height: 20px;
    background: #2a2a40;
    border-radius: 10px;
    overflow: hidden;
    margin-bottom: 5px;
  }

  .progress-bar-fill {
    height: 100%;
    background: #666;
    transition: width 0.3s ease;
  }

  .progress-bar-fill.active {
    background: linear-gradient(90deg, #ef4444, #facc15, #22c55e);
  }

  .confidence-label {
    display: flex;
    justify-content: space-between;
    font-size: 0.9rem;
    color: #aaa;
  }

  /* Calibration Panel Styles */
  .controls-column {
    display: flex;
    flex-direction: column;
    gap: 25px;
  }

  .instructions {
    font-size: 0.9rem;
    color: #aaa;
    margin-bottom: 20px;
  }

  .calibration-form {
    display: flex;
    flex-direction: column;
    gap: 15px;
    margin-bottom: 30px;
  }

  select, button {
    padding: 12px;
    border-radius: 6px;
    font-size: 1rem;
  }

  select {
    background: #0f3460;
    border: 1px solid #4cc9f0;
    color: white;
  }

  .calibrate-btn {
    background: #4cc9f0;
    color: #000;
    border: none;
    font-weight: bold;
    cursor: pointer;
    transition: all 0.2s;
  }

  .calibrate-btn:hover:not(:disabled) {
    background: #3db5e0;
    transform: translateY(-2px);
  }

  .calibrate-btn:disabled {
    background: #555;
    cursor: not-allowed;
    opacity: 0.7;
  }

  .calibrate-btn.loading {
    background: #facc15;
  }

  .tags {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-top: 15px;
  }

  .tag {
    padding: 5px 10px;
    border-radius: 20px;
    font-size: 0.85rem;
    background: #2a2a40;
    color: #666;
    border: 1px solid #3a3a50;
  }

  .tag.active {
    background: #4cc9f022;
    color: #4cc9f0;
    border-color: #4cc9f0;
  }

  .stream-info {
    display: flex;
    justify-content: space-between;
    margin-top: 10px;
    font-size: 0.8rem;
    color: #888;
  }

  /* ✅ ESTILOS PARA NOTIFICACIONES */
  .notification {
    position: fixed;
    top: 20px;
    right: 20px;
    padding: 15px 20px;
    border-radius: 8px;
    color: white;
    font-weight: bold;
    z-index: 1000;
    box-shadow: 0 4px 12px rgba(0,0,0,0.3);
    animation: slideIn 0.3s ease-out;
    max-width: 300px;
  }

  .notification.success {
    background: #22c55e;
    border-left: 4px solid #16a34a;
  }

  .notification.error {
    background: #ef4444;
    border-left: 4px solid #dc2626;
  }

  .notification.info {
    background: #3b82f6;
    border-left: 4px solid #2563eb;
  }

  @keyframes slideIn {
    from {
      transform: translateX(100%);
      opacity: 0;
    }
    to {
      transform: translateX(0);
      opacity: 1;
    }
  }
</style>