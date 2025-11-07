<script>
    import { onMount, onDestroy } from 'svelte';

    const DEVICE_IP = '192.168.1.84';
    const BASE = `http://${DEVICE_IP}`;

    let isLoading = false;
    let statusMessage = '';
    let connected = false;
    let lastStatus = null;
    let detection = { figura: 'Ninguna', confianza: 0 };
    let statusInterval;

    async function fetchStatus() {
        try {
            const res = await fetch(`${BASE}/status`);
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            const data = await res.json();
            lastStatus = data;
            connected = data.conectado ?? true;
            statusMessage = 'Estado actualizado';
        } catch (err) {
            connected = false;
            statusMessage = `No se pudo obtener estado: ${err.message}`;
        }
    }

    async function postCalibrate(id) {
        isLoading = true;
        statusMessage = `Calibrando figura ${id}...`;
        try {
            const res = await fetch(`${BASE}/calibrate`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ figure: id })
            });
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            statusMessage = `Calibración (${id}) enviada`;
        } catch (err) {
            statusMessage = `Error calibrando: ${err.message}`;
        } finally {
            isLoading = false;
        }
    }

async function getDetect() {
    isLoading = true;
    statusMessage = 'Solicitando detección...';
    try {
        const res = await fetch(`${BASE}/detect`);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data = await res.json();
        detection = {
            figura: data.figura ?? data.name ?? JSON.stringify(data),
            confianza: Number(data.confianza ?? data.confidence ?? 0)
        };
        // CAMBIO AQUÍ: Mostrar directamente la detección
        statusMessage = `Detección: ${detection.figura} (${(detection.confianza * 100).toFixed(1)}%)`;
    } catch (err) {
        statusMessage = `Error en detección: ${err.message}`;
    } finally {
        isLoading = false;
    }
}
    onMount(() => {
        fetchStatus();
        statusInterval = setInterval(fetchStatus, 5000);
    });

    onDestroy(() => {
        clearInterval(statusInterval);
    });
</script>

<svelte:head>
    <title>Detector de Figuras - ESP32-CAM</title>
    <meta name="viewport" content="width=device-width,initial-scale=1" />
</svelte:head>

<main>
    <div class="container">
        <h1>Detector de Figuras (192.168.1.84)</h1>

        <div class="section">
            <h2>Estado del Sistema</h2>
            <div class="status">
                <div class="status-item">
                    <span class="label">Conectado:</span>
                    <span class="value {connected ? 'connected' : 'disconnected'}">
                        {connected ? 'SÍ' : 'NO'}
                    </span>
                </div>
                <div class="status-item">
                    <span class="label">Último estado:</span>
                    <span class="value">{#if lastStatus}{JSON.stringify(lastStatus)}{:else}—{/if}</span>
                </div>
            </div>
            <div style="margin-top:10px">
                <button on:click={fetchStatus} disabled={isLoading}>Actualizar estado</button>
            </div>
        </div>

        <div class="section">
            <h2>Calibración de Figuras</h2>
            <p>Coloca cada figura frente a la cámara y haz clic en el botón correspondiente (0-4):</p>
            <div class="calibration-buttons">
                <button on:click={() => postCalibrate(0)} disabled={isLoading || !connected}>0 - Cuadrado</button>
                <button on:click={() => postCalibrate(1)} disabled={isLoading || !connected}>1 - Círculo</button>
                <button on:click={() => postCalibrate(2)} disabled={isLoading || !connected}>2 - L</button>
                <button on:click={() => postCalibrate(3)} disabled={isLoading || !connected}>3 - Estrella</button>
                <button on:click={() => postCalibrate(4)} disabled={isLoading || !connected}>4 - C</button>
            </div>
        </div>

        <div class="section">
            <h2>Detección</h2>
            <div class="detection-controls">
                <button on:click={getDetect} disabled={isLoading || !connected}>{isLoading ? 'Procesando...' : 'Detectar figura'}</button>
            </div>

            {#if detection.figura !== 'Ninguna'}
                <div class="detection-result">
                    <h3>Resultado</h3>
                    <div class="result-item"><span class="label">Figura:</span><span class="value">{detection.figura}</span></div>
                    <div class="result-item"><span class="label">Confianza:</span><span class="value">{(detection.confianza * 100).toFixed(1)}%</span></div>
                </div>
            {/if}
        </div>

        <div class="section">
    <h2>Vista en Vivo</h2>
    {#if connected}
        <div class="video-container">
            <img src={`${BASE}/stream`} alt="Stream de la cámara" class="video-stream-large" />
        </div>
    {:else}
        <p class="no-video">Conecta la ESP32-CAM para ver el video</p>
    {/if}
</div>

<style>
    .video-container { 
        text-align: center;
        margin: 20px 0;
    }
    .video-stream-large { 
        width: 95%;
        max-width: 850px;
        height: 480px;
        border: 3px solid #3498db;
        border-radius: 8px;
        object-fit: cover;
        box-shadow: 0 4px 12px rgba(0,0,0,0.15);
    }
    .no-video { 
        text-align: center; 
        color: #7f8c8d; 
        font-style: italic;
        padding: 40px;
        font-size: 18px;
    }
</style>

        {#if statusMessage}
            <div class="status-message">{statusMessage}</div>
        {/if}
    </div>
</main>

<style>
    :global(body) { margin: 0; padding: 0; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #f5f5f5; color: #333; }
    .container { max-width: 900px; margin: 0 auto; padding: 20px; }
    h1 { text-align: center; color: #2c3e50; margin-bottom: 20px; }
    h2 { color: #34495e; border-bottom: 2px solid #3498db; padding-bottom: 6px; margin-top: 20px; }
    .section { background: white; padding: 16px; margin-bottom: 16px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.08); }
    .calibration-buttons { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 10px; margin-top: 10px; }
    .status, .detection-result { display: grid; gap: 8px; }
    .status-item, .result-item { display:flex; justify-content:space-between; align-items:center; padding:6px 0; border-bottom:1px solid #eee; }
    .label { font-weight:600; }
    .value.connected { color: #27ae60; font-weight:700; }
    .value.disconnected { color: #e74c3c; font-weight:700; }
    .video-container { text-align:center; }
    .video-stream { max-width:100%; border:2px solid #3498db; border-radius:6px; }
    .no-video { text-align:center; color:#7f8c8d; font-style:italic; }
    .status-message { background:#e8f4fd; border-left:4px solid #3498db; padding:10px 12px; margin-top:12px; border-radius:4px; }
    button { background-color:#3498db; color:white; border:none; padding:10px 14px; border-radius:6px; cursor:pointer; font-size:14px; }
    button:disabled { background-color:#bdc3c7; cursor:not-allowed; }
</style>
