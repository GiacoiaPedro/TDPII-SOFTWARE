<script>
  import { onMount, onDestroy } from 'svelte';

  // ===== CONFIGURACIÓN =====
  let espIp = '192.168.1.84';
  let base_url = `http://${espIp}`;

  // ===== ESTADO DE LA APP =====
  let streamUrl = `${base_url}/processed_stream`;
  let isConnected = false;
  let isCalibrating = false;
  let lastUpdate = new Date();
  
  // Datos de detección
  let detectionData = {
    figura: "Esperando...",
    confianza: 0
  };

  // Estado de calibración
let figurasPosibles = [
  { id: 0, nombre: "Cuadrado", icon: "⬜" },
  { id: 1, nombre: "Círculo", icon: "⚪" },
  { id: 2, nombre: "L", icon: "🅻" },      // representa forma en ángulo, tipo "L" o esquina
  { id: 3, nombre: "Estrella", icon: "⭐" },
  { id: 4, nombre: "C", icon: "🅒" }       // forma semicircular como una "C"
];

  
  let figurasCalibradas = [];
  let selectedFigureId = 0;

  // Sistema de notificaciones
  let notification = {
    show: false,
    message: '',
    type: 'info' // 'success', 'error', 'info'
  };

  // Timers
  let detectionInterval;
  let statusInterval;

  onMount(() => {
    updateBaseUrl();
    startPolling();
  });

  onDestroy(() => {
    stopPolling();
  });

  function updateBaseUrl() {
    const cleanIp = espIp.replace(/\/$/, '');
    base_url = cleanIp.startsWith('http') ? cleanIp : `http://${cleanIp}`;
    refreshStream();
  }

  function refreshStream() {
    streamUrl = `${base_url}/processed_stream?t=${Date.now()}`;
  }

  function startPolling() {
    stopPolling();
    detectionInterval = setInterval(fetchDetection, 200);
    statusInterval = setInterval(fetchStatus, 2000);
  }

  function stopPolling() {
    clearInterval(detectionInterval);
    clearInterval(statusInterval);
  }

  async function fetchDetection() {
    try {
      const res = await fetch(`${base_url}/detect`);
      if (res.ok) {
        detectionData = await res.json();
        lastUpdate = new Date();
        isConnected = true;
      }
    } catch (e) {
      isConnected = false;
    }
  }

  async function fetchStatus() {
    try {
      const res = await fetch(`${base_url}/status`);
      if (res.ok) {
        const data = await res.json();
        figurasCalibradas = data.figuras_calibradas || [];
        isConnected = true;
      }
    } catch (e) {
      isConnected = false;
    }
  }

  // Función para mostrar notificaciones
  function showNotification(message, type = 'info') {
    notification = { show: true, message, type };
    // Auto-ocultar después de 4 segundos
    setTimeout(() => {
      notification.show = false;
    }, 4000);
  }

  async function calibrarFigura() {
    if (isCalibrating) return;
    isCalibrating = true;

    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 8000); // 8 segundos

    try {
        console.log('🔧 Iniciando calibración para figura:', selectedFigureId);
        console.log('📤 Enviando a:', `${base_url}/calibrate`);
        
        const res = await fetch(`${base_url}/calibrate`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({ figure: selectedFigureId }),
            signal: controller.signal
        });

        clearTimeout(timeoutId);
        
        console.log('📡 Respuesta HTTP - Status:', res.status, res.statusText);
        
        const responseText = await res.text();
        console.log('📄 Respuesta cruda:', responseText);
        
        if (res.ok) {
            try {
                const responseData = JSON.parse(responseText);
                console.log('✅ Respuesta JSON parseada:', responseData);
                
                // Actualización inmediata
                const figuraCalibrada = figurasPosibles[selectedFigureId].nombre;
                if (!figurasCalibradas.includes(figuraCalibrada)) {
                    figurasCalibradas = [...figurasCalibradas, figuraCalibrada];
                }
                
                // ✅ REEMPLAZAR ALERT POR NOTIFICACIÓN
                showNotification(`✅ ${figuraCalibrada} calibrado correctamente`, 'success');
                await fetchStatus(); // Sincronizar con servidor
            } catch (parseError) {
                console.error('❌ Error parseando JSON:', parseError);
                showNotification('❌ Error: Respuesta no válida del servidor', 'error');
                throw new Error('Respuesta no es JSON válido: ' + responseText);
            }
        } else {
            console.error('❌ Error del servidor. Status:', res.status, 'Texto:', responseText);
            showNotification(`❌ Error del servidor: ${res.status}`, 'error');
            throw new Error(`Error ${res.status}: ${responseText}`);
        }
    } catch (e) {
        if (e.name === 'AbortError') {
            showNotification('❌ Timeout: La ESP32 no respondió en 8 segundos', 'error');
        } else {
            showNotification(`❌ Error al calibrar: ${e.message}`, 'error');
        }
        console.error('💥 Error en calibración:', e);
    } finally {
        isCalibrating = false;
        console.log('🏁 Calibración finalizada - Estado isCalibrating:', isCalibrating);
    }
  }

  // Formatear porcentaje para la UI
  $: confianzaPorcentaje = (detectionData.confianza * 100).toFixed(1);
  $: confianzaColor = detectionData.confianza > 0.8 ? '#4ade80' : 
                      detectionData.confianza > 0.5 ? '#facc15' : '#ef4444';

</script>

<main class="container">
  <header>
    <h1>👁️ ESP32 Vision System</h1>
    <div class="connection-bar" class:connected={isConnected}>
      <span>Estado: {isConnected ? 'CONECTADO' : 'DESCONECTADO'}</span>
      <div class="ip-config">
        <input type="text" bind:value={espIp} on:change={updateBaseUrl} placeholder="IP del ESP32" />
        <button on:click={refreshStream} title="Recargar Stream">🔄</button>
      </div>
    </div>
  </header>

  <!-- ✅ SISTEMA DE NOTIFICACIONES -->
  {#if notification.show}
    <div class="notification {notification.type}">
      {notification.message}
    </div>
  {/if}

  <div class="grid-layout">
    <div class="panel stream-panel">
      <h2>Stream Procesado (Binario)</h2>
      <div class="video-container">
        {#if isConnected}
          <img src={streamUrl} alt="Stream procesado en tiempo real" />
        {:else}
          <div class="placeholder">
            <p>💤 Esperando conexión con {espIp}...</p>
          </div>
        {/if}
      </div>
      <div class="stream-info">
        <small>Última actualización: {lastUpdate.toLocaleTimeString()}</small>
        <small>Vista: Imagen binaria procesada</small>
      </div>
    </div>

    <div class="controls-column">
      
      <div class="panel detection-panel">
        <h2>🎯 Detección Actual</h2>
        <div class="detection-result">
          <span class="label">Figura:</span>
          <span class="value figure-name">{detectionData.figura}</span>
        </div>
        
        <div class="confidence-meter">
          <div class="label">
            <span>Confianza</span>
            <span>{confianzaPorcentaje}%</span>
          </div>
          <div class="progress-bar-bg">
            <div class="progress-bar-fill" 
                 style="width: {confianzaPorcentaje}%; background-color: {confianzaColor}">
            </div>
          </div>
        </div>
      </div>

      <div class="panel calibration-panel">
        <h2>⚙️ Calibración</h2>
        <p class="instructions">Coloca una figura frente a la cámara y selecciona cuál es para entrenar el sistema.</p>
        
        <div class="calibration-form">
          <select bind:value={selectedFigureId} disabled={isCalibrating}>
            {#each figurasPosibles as fig}
              <option value={fig.id}>
                {fig.icon} {fig.nombre} 
              </option>
            {/each}
          </select>
          
          <button 
            class="calibrate-btn" 
            class:loading={isCalibrating}
            disabled={!isConnected || isCalibrating}
            on:click={calibrarFigura}
          >
            {isCalibrating ? 'Calibrando...' : '📸 CALIBRAR AHORA'}
          </button>
        </div>

        <div class="calibrated-list">
          <h3>Estado del Sistema:</h3>
          <div class="tags">
            {#each figurasPosibles as fig}
              <span class="tag" class:active={figurasCalibradas.includes(fig.nombre)}>
                {fig.icon} {fig.nombre}
              </span>
            {/each}
          </div>
        </div>
      </div>

    </div>
  </div>
</main>

<style>
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

  .ip-config {
    display: flex;
    gap: 10px;
  }

  .ip-config input {
    background: #2a2a40;
    border: 1px solid #3a3a50;
    color: #fff;
    padding: 5px 10px;
    border-radius: 4px;
    width: 150px;
  }

  .grid-layout {
    display: grid;
    grid-template-columns: 1.5fr 1fr;
    gap: 25px;
  }

  @media (max-width: 768px) {
    .grid-layout { grid-template-columns: 1fr; }
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

  /* Detection Panel Styles */
  .detection-result {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 20px;
    font-size: 1.2rem;
  }

  .figure-name {
    font-weight: bold;
    color: #fff;
    font-size: 1.5rem;
  }

  .progress-bar-bg {
    height: 25px;
    background: #0f3460;
    border-radius: 12px;
    overflow: hidden;
    margin-top: 5px;
  }

  .progress-bar-fill {
    height: 100%;
    transition: width 0.3s ease, background-color 0.3s ease;
  }

  .confidence-meter .label {
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