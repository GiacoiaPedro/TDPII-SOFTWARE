<script>
  import { onMount, onDestroy } from 'svelte';

  let ip = "http://192.168.1.84";
  let datos = null;
  let cargando = false;
  let actualizando = false;
  let intervalo;

  // Variables para las rotaciones y zoom
  let rotacionImagen1 = 0;
  let rotacionImagen2 = 0;
  let zoomImagen1 = 1;
  let zoomImagen2 = 1;
  let posicionZoom1 = { x: '50%', y: '50%' };
  let posicionZoom2 = { x: '50%', y: '50%' };

  // Variables para controlar la visibilidad de imágenes
  let mostrarImagenCirculoCuadrado = false;
  let mostrarImagenTrianguloL = false;

  // Objetos detectados por forma (no por ID)
  let circulo = null;
  let cuadrado = null;
  let triangulo = null;
  let L = null;

  // Parámetros de normalización
  const X_MIN = 55;
  const X_MAX = 300;
  const Y_MIN = 55;
  const Y_MAX = 250;
  
  // Tamaño de visualización de la imagen en 16:9
  const DISPLAY_WIDTH = 320;
  const DISPLAY_HEIGHT = 180;

  // Función para verificar si un objeto es real
  function esObjetoReal(objeto) {
    return objeto && 
           objeto.detectado === true && 
           objeto.area > 0 && 
           objeto.centro_x > 0 && 
           objeto.centro_y > 0;
  }

  // Función para normalizar coordenadas
  function normalizarCoordenadas(x, y) {
    let x_norm = (x - X_MIN) / (X_MAX - X_MIN);
    let y_norm = (y - Y_MIN) / (Y_MAX - Y_MIN);
    
    x_norm = Math.max(0, Math.min(1, x_norm));
    y_norm = Math.max(0, Math.min(1, y_norm));
    
    return { x: x_norm, y: y_norm };
  }

  // Función para calcular zoom basado en distancia
  function calcularZoom(distanciaNorm) {
    const zoomMin = 1.0;
    const zoomMax = 3.0;
    const factor = Math.exp(-2 * distanciaNorm);
    return zoomMin + (zoomMax - zoomMin) * factor;
  }

  // Función para calcular posición de zoom en píxeles de pantalla
  function calcularPosicionZoom(normX, normY) {
    const x_px = normX * DISPLAY_WIDTH;
    const y_px = normY * DISPLAY_HEIGHT;
    
    return { 
      x: `${x_px}px`, 
      y: `${y_px}px` 
    };
  }

  // Función para calcular ángulo entre dos puntos (0-360 grados)
  function calcularAnguloDesdePunto1(x1, y1, x2, y2) {
    // Calcular diferencia de coordenadas
    const dx = x2 - x1;
    const dy = y2 - y1;
    
    // Calcular ángulo en radianes
    const anguloRad = Math.atan2(dy, dx);
    
    // Convertir a grados y normalizar a 0-360
    let anguloGrados = anguloRad * (180 / Math.PI);
    
    // Asegurar que esté entre 0 y 360
    anguloGrados = (anguloGrados + 360) % 360;
    
    return anguloGrados;
  }

  // Función para encontrar objeto por forma (más estricta)
  function encontrarObjetoPorForma(objetos, formaBuscada) {
    const formaBuscadaNorm = formaBuscada.toLowerCase().trim();
    
    return objetos.find(objeto => {
      if (!objeto || objeto.detectado !== true || !objeto.forma) return false;
      
      const formaObjeto = objeto.forma.toLowerCase().trim();
      
      // Para la L, requerir coincidencia exacta
      if (formaBuscadaNorm === 'l') {
        return formaObjeto === 'l';
      }
      // Para otras formas, permitir que contengan la palabra
      else if (formaBuscadaNorm === 'circulo' || formaBuscadaNorm === 'círculo') {
        return formaObjeto.includes('circulo') || formaObjeto.includes('círculo');
      }
      else if (formaBuscadaNorm === 'cuadrado') {
        return formaObjeto.includes('cuadrado');
      }
      else if (formaBuscadaNorm === 'triangulo' || formaBuscadaNorm === 'triángulo') {
        return formaObjeto.includes('triangulo') || formaObjeto.includes('triángulo');
      }
      
      return false;
    });
  }

  // Función auxiliar para mostrar nombre de forma con acentos correctos
  function obtenerNombreForma(forma) {
    if (!forma) return "No detectado";
    
    const formaLower = forma.toLowerCase().trim();
    
    // Mapeo de formas
    if (formaLower === 'l') return "L";
    if (formaLower.includes('circulo') || formaLower.includes('círculo')) return "Círculo";
    if (formaLower.includes('cuadrado')) return "Cuadrado";
    if (formaLower.includes('triangulo') || formaLower.includes('triángulo')) return "Triángulo";
    
    return forma; // Devolver original si no coincide
  }

  async function obtenerDatos() {
    if (cargando) return;
    
    cargando = true;
    try {
      const response = await fetch(`${ip}/status`);
      datos = await response.json();
      
      if (datos && datos.objetos) {
        // Buscar objetos por FORMA (no por ID)
        const objetosDetectados = datos.objetos.filter(o => o.detectado === true);
        
        // Buscar cada forma
        circulo = encontrarObjetoPorForma(objetosDetectados, "circulo");
        cuadrado = encontrarObjetoPorForma(objetosDetectados, "cuadrado");
        triangulo = encontrarObjetoPorForma(objetosDetectados, "triangulo");
        L = encontrarObjetoPorForma(objetosDetectados, "l");
        
        // Verificar par 1: Círculo y Cuadrado (por FORMA, no por ID)
        mostrarImagenCirculoCuadrado = esObjetoReal(circulo) && esObjetoReal(cuadrado);
        
        // Verificar par 2: Triángulo y L (por FORMA, no por ID)
        mostrarImagenTrianguloL = esObjetoReal(triangulo) && esObjetoReal(L);
        
        // Calcular relaciones si hay pares detectados
        if (mostrarImagenCirculoCuadrado || mostrarImagenTrianguloL) {
          calcularRelaciones();
        } else {
          resetearValores();
        }
      }
    } catch (error) {
      console.error("Error:", error);
      mostrarImagenCirculoCuadrado = false;
      mostrarImagenTrianguloL = false;
      resetearValores();
    }
    cargando = false;
  }

  function resetearValores() {
    rotacionImagen1 = 0;
    rotacionImagen2 = 0;
    zoomImagen1 = 1;
    zoomImagen2 = 1;
    posicionZoom1 = { x: '50%', y: '50%' };
    posicionZoom2 = { x: '50%', y: '50%' };
  }

  function calcularRelaciones() {
    // Calcular relación entre Círculo y Cuadrado (PAR 1)
    if (circulo && cuadrado && esObjetoReal(circulo) && esObjetoReal(cuadrado)) {
      // Normalizar coordenadas
      const normCirculo = normalizarCoordenadas(circulo.centro_x, circulo.centro_y);
      const normCuadrado = normalizarCoordenadas(cuadrado.centro_x, cuadrado.centro_y);
      
      // Calcular distancia en espacio normalizado
      const dx = normCuadrado.x - normCirculo.x;
      const dy = normCuadrado.y - normCirculo.y;
      const distanciaNorm = Math.sqrt(dx * dx + dy * dy);
      
      // Calcular ángulo desde el Círculo hacia el Cuadrado (0-360°)
      rotacionImagen1 = calcularAnguloDesdePunto1(
        circulo.centro_x, circulo.centro_y,
        cuadrado.centro_x, cuadrado.centro_y
      );
      
      // Calcular zoom basado en distancia normalizada
      zoomImagen1 = calcularZoom(distanciaNorm);
      
      // Calcular punto medio para el centro del zoom
      const midX = (normCirculo.x + normCuadrado.x) / 2;
      const midY = (normCirculo.y + normCuadrado.y) / 2;
      
      // Calcular posición de zoom en píxeles
      posicionZoom1 = calcularPosicionZoom(midX, midY);
    } else {
      rotacionImagen1 = 0;
      zoomImagen1 = 1;
      posicionZoom1 = { x: '50%', y: '50%' };
    }

    // Calcular relación entre Triángulo y L (PAR 2)
    if (triangulo && L && esObjetoReal(triangulo) && esObjetoReal(L)) {
      // Normalizar coordenadas
      const normTriangulo = normalizarCoordenadas(triangulo.centro_x, triangulo.centro_y);
      const normL = normalizarCoordenadas(L.centro_x, L.centro_y);
      
      // Calcular distancia en espacio normalizado
      const dx = normL.x - normTriangulo.x;
      const dy = normL.y - normTriangulo.y;
      const distanciaNorm = Math.sqrt(dx * dx + dy * dy);
      
      // Calcular ángulo desde el Triángulo hacia la L (0-360°)
      rotacionImagen2 = calcularAnguloDesdePunto1(
        triangulo.centro_x, triangulo.centro_y,
        L.centro_x, L.centro_y
      );
      
      // Calcular zoom basado en distancia normalizada
      zoomImagen2 = calcularZoom(distanciaNorm);
      
      // Calcular punto medio para el centro del zoom
      const midX = (normTriangulo.x + normL.x) / 2;
      const midY = (normTriangulo.y + normL.y) / 2;
      
      // Calcular posición de zoom en píxeles
      posicionZoom2 = calcularPosicionZoom(midX, midY);
    } else {
      rotacionImagen2 = 0;
      zoomImagen2 = 1;
      posicionZoom2 = { x: '50%', y: '50%' };
    }
  }

  // Calcular número de objetos detectados
  $: objetosDetectados = [circulo, cuadrado, triangulo, L].filter(o => esObjetoReal(o)).length;

  function iniciarActualizacionAutomatica() {
    if (intervalo) clearInterval(intervalo);
    actualizando = true;
    obtenerDatos();
    intervalo = setInterval(obtenerDatos, 1000);
  }

  function detenerActualizacionAutomatica() {
    actualizando = false;
    if (intervalo) {
      clearInterval(intervalo);
      intervalo = null;
    }
  }

  function toggleActualizacionAutomatica() {
    actualizando ? detenerActualizacionAutomatica() : iniciarActualizacionAutomatica();
  }

  onMount(iniciarActualizacionAutomatica);
  onDestroy(detenerActualizacionAutomatica);
</script>

<div class="container">
  <h1>Detección de Formas Geométricas</h1>
  
  <!-- Controles -->
  <div class="control-centered">
    <input 
      type="text" 
      bind:value={ip} 
      placeholder="IP del ESP32"
    />
    <button on:click={obtenerDatos} disabled={cargando}>
      {cargando ? "🔄 Cargando..." : "📡 Actualizar"}
    </button>
    <button on:click={toggleActualizacionAutomatica} class={actualizando ? 'btn-activo' : 'btn-inactivo'}>
      {actualizando ? '⏸️ Pausar' : '▶️ Auto'}
    </button>
  </div>

  <!-- Imágenes con rotación y zoom -->
  <div class="images-container">
    
    <!-- Imagen 1 (Círculo y Cuadrado) -->
    <div class="image-with-controls">
      {#if mostrarImagenCirculoCuadrado}
        <div class="image-container image-16-9">
          <img 
            src="/imagen2.jpg" 
            alt="Círculo y Cuadrado" 
            style="
              transform: rotate({rotacionImagen1}deg) scale({zoomImagen1});
              transform-origin: {posicionZoom1.x} {posicionZoom1.y};
              transition: transform 0.5s ease;
            "
            class="rotatable-image"
          />
        </div>
        <div class="info-container">
          <div class="zoom-info">
            <span>Zoom: {zoomImagen1.toFixed(2)}x</span>
            <span class="separador">|</span>
            <span>Ángulo: {rotacionImagen1.toFixed(1)}°</span>
          </div>
          <div class="relacion-info">
            <span>Relación: Círculo → Cuadrado</span>
          </div>
        </div>
      {:else}
        <div class="no-detection-message">
          <div class="no-detection-icon">🔍</div>
          <p>Círculo o Cuadrado no detectados</p>
          <p class="rango-info">Esperando objetos del Par 1</p>
        </div>
      {/if}
    </div>
    
    <!-- Imagen 2 (Triángulo y L) -->
    <div class="image-with-controls">
      {#if mostrarImagenTrianguloL}
        <div class="image-container image-16-9">
          <img 
            src="/imagen2.jpg" 
            alt="Triángulo y L" 
            style="
              transform: rotate({rotacionImagen2}deg) scale({zoomImagen2});
              transform-origin: {posicionZoom2.x} {posicionZoom2.y};
              transition: transform 0.5s ease;
            "
            class="rotatable-image"
          />
        </div>
        <div class="info-container">
          <div class="zoom-info">
            <span>Zoom: {zoomImagen2.toFixed(2)}x</span>
            <span class="separador">|</span>
            <span>Ángulo: {rotacionImagen2.toFixed(1)}°</span>
          </div>
          <div class="relacion-info">
            <span>Relación: Triángulo → L</span>
          </div>
        </div>
      {:else}
        <div class="no-detection-message">
          <div class="no-detection-icon">🔍</div>
          <p>Triángulo o L no detectados</p>
          <p class="rango-info">Esperando objetos del Par 2</p>
        </div>
      {/if}
    </div>
  </div>

  <!-- Resumen de detecciones -->
  {#if datos}
    <div class="deteccion-resumen">
      <h3>Estado de Formas Detectadas</h3>
      <div class="resumen-columnas">
        <!-- Columna 1: Círculo y Cuadrado -->
        <div class="columna">
          <div class="columna-titulo">Par 1: Círculo y Cuadrado</div>
          
          <!-- Círculo -->
          <div class="objeto-card {circulo && esObjetoReal(circulo) ? 'detectado' : 'no-detectado'}">
            <div class="objeto-header">
              <span class="objeto-nombre">Círculo</span>
              <span class="objeto-estado">
                {#if circulo && esObjetoReal(circulo)}
                  ✅ Detectado
                {:else}
                  ❌ No detectado
                {/if}
              </span>
            </div>
            {#if circulo && esObjetoReal(circulo)}
              <div class="objeto-detalles">
                <p><strong>Forma:</strong> {obtenerNombreForma(circulo.forma)}</p>
                <p><strong>Color:</strong> {circulo.color || "N/A"}</p>
                <p><strong>Área:</strong> {circulo.area > 0 ? circulo.area : "N/A"}</p>
                <p><strong>Posición:</strong> ({circulo.centro_x}, {circulo.centro_y})</p>
                <p><strong>Bounding Box:</strong> {circulo.bbox_w}x{circulo.bbox_h}</p>
              </div>
            {:else}
              <div class="objeto-detalles">
                <p><em>Círculo no detectado en la escena</em></p>
              </div>
            {/if}
          </div>
          
          <!-- Cuadrado -->
          <div class="objeto-card {cuadrado && esObjetoReal(cuadrado) ? 'detectado' : 'no-detectado'}">
            <div class="objeto-header">
              <span class="objeto-nombre">Cuadrado</span>
              <span class="objeto-estado">
                {#if cuadrado && esObjetoReal(cuadrado)}
                  ✅ Detectado
                {:else}
                  ❌ No detectado
                {/if}
              </span>
            </div>
            {#if cuadrado && esObjetoReal(cuadrado)}
              <div class="objeto-detalles">
                <p><strong>Forma:</strong> {obtenerNombreForma(cuadrado.forma)}</p>
                <p><strong>Color:</strong> {cuadrado.color || "N/A"}</p>
                <p><strong>Área:</strong> {cuadrado.area > 0 ? cuadrado.area : "N/A"}</p>
                <p><strong>Posición:</strong> ({cuadrado.centro_x}, {cuadrado.centro_y})</p>
                <p><strong>Bounding Box:</strong> {cuadrado.bbox_w}x{cuadrado.bbox_h}</p>
              </div>
            {:else}
              <div class="objeto-detalles">
                <p><em>Cuadrado no detectado en la escena</em></p>
              </div>
            {/if}
          </div>
        </div>
        
        <!-- Columna 2: Triángulo y L -->
        <div class="columna">
          <div class="columna-titulo">Par 2: Triángulo y L</div>
          
          <!-- Triángulo -->
          <div class="objeto-card {triangulo && esObjetoReal(triangulo) ? 'detectado' : 'no-detectado'}">
            <div class="objeto-header">
              <span class="objeto-nombre">Triángulo</span>
              <span class="objeto-estado">
                {#if triangulo && esObjetoReal(triangulo)}
                  ✅ Detectado
                {:else}
                  ❌ No detectado
                {/if}
              </span>
            </div>
            {#if triangulo && esObjetoReal(triangulo)}
              <div class="objeto-detalles">
                <p><strong>Forma:</strong> {obtenerNombreForma(triangulo.forma)}</p>
                <p><strong>Color:</strong> {triangulo.color || "N/A"}</p>
                <p><strong>Área:</strong> {triangulo.area > 0 ? triangulo.area : "N/A"}</p>
                <p><strong>Posición:</strong> ({triangulo.centro_x}, {triangulo.centro_y})</p>
                <p><strong>Bounding Box:</strong> {triangulo.bbox_w}x{triangulo.bbox_h}</p>
              </div>
            {:else}
              <div class="objeto-detalles">
                <p><em>Triángulo no detectado en la escena</em></p>
              </div>
            {/if}
          </div>
          
          <!-- L -->
          <div class="objeto-card {L && esObjetoReal(L) ? 'detectado' : 'no-detectado'}">
            <div class="objeto-header">
              <span class="objeto-nombre">L</span>
              <span class="objeto-estado">
                {#if L && esObjetoReal(L)}
                  ✅ Detectado
                {:else}
                  ❌ No detectado
                {/if}
              </span>
            </div>
            {#if L && esObjetoReal(L)}
              <div class="objeto-detalles">
                <p><strong>Forma:</strong> {obtenerNombreForma(L.forma)}</p>
                <p><strong>Color:</strong> {L.color || "N/A"}</p>
                <p><strong>Área:</strong> {L.area > 0 ? L.area : "N/A"}</p>
                <p><strong>Posición:</strong> ({L.centro_x}, {L.centro_y})</p>
                <p><strong>Bounding Box:</strong> {L.bbox_w}x{L.bbox_h}</p>
              </div>
            {:else}
              <div class="objeto-detalles">
                <p><em>L no detectado en la escena</em></p>
              </div>
            {/if}
          </div>
        </div>
      </div>
      <div class="total-objetos">
        <p>Formas detectadas: <strong>{objetosDetectados}</strong> de 4</p>
        <p class="rango-total">Rango de trabajo normalizado: X: {X_MIN}-{X_MAX}px, Y: {Y_MIN}-{Y_MAX}px</p>
      </div>
    </div>
  {/if}

  <!-- Relaciones calculadas -->
  {#if datos && (mostrarImagenCirculoCuadrado || mostrarImagenTrianguloL)}
    <div class="relaciones">
      <h3>Relaciones Entre Formas</h3>
      
      {#if mostrarImagenCirculoCuadrado}
        <div class="relacion-grupo">
          <h4>Círculo → Cuadrado (Par 1):</h4>
          <div class="relacion-datos">
            <p><strong>Ángulo de referencia:</strong> Desde Círculo hacia Cuadrado</p>
            <p><strong>Ángulo calculado:</strong> {rotacionImagen1.toFixed(2)}° (0-360°)</p>
            <p><strong>Zoom aplicado:</strong> {zoomImagen1.toFixed(2)}x</p>
            <p><strong>Centro de transformación:</strong> ({posicionZoom1.x}, {posicionZoom1.y})</p>
            {#if circulo && cuadrado}
              <p><strong>Posición Círculo:</strong> ({circulo.centro_x}, {circulo.centro_y})</p>
              <p><strong>Posición Cuadrado:</strong> ({cuadrado.centro_x}, {cuadrado.centro_y})</p>
            {/if}
          </div>
        </div>
      {/if}
      
      {#if mostrarImagenTrianguloL}
        <div class="relacion-grupo">
          <h4>Triángulo → L (Par 2):</h4>
          <div class="relacion-datos">
            <p><strong>Ángulo de referencia:</strong> Desde Triángulo hacia L</p>
            <p><strong>Ángulo calculado:</strong> {rotacionImagen2.toFixed(2)}° (0-360°)</p>
            <p><strong>Zoom aplicado:</strong> {zoomImagen2.toFixed(2)}x</p>
            <p><strong>Centro de transformación:</strong> ({posicionZoom2.x}, {posicionZoom2.y})</p>
            {#if triangulo && L}
              <p><strong>Posición Triángulo:</strong> ({triangulo.centro_x}, {triangulo.centro_y})</p>
              <p><strong>Posición L:</strong> ({L.centro_x}, {L.centro_y})</p>
            {/if}
          </div>
        </div>
      {/if}
    </div>
  {/if}

  <!-- Datos JSON -->
  {#if datos}
    <div class="datos">
      <h3>Datos JSON Recibidos</h3>
      <pre>{JSON.stringify(datos, null, 2)}</pre>
    </div>
  {/if}
</div>

<style>
  :global(body) {
    margin: 0;
    padding: 0;
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    background-color: #121212;
    color: #e0e0e0;
  }

  .container {
    max-width: 1200px;
    margin: 0 auto;
    padding: 20px;
  }

  h1 {
    color: #ffffff;
    text-align: center;
    margin-bottom: 30px;
    border-bottom: 2px solid #333;
    padding-bottom: 15px;
  }

  h3 {
    color: #ffffff;
    margin-top: 0;
  }

  .control-centered {
    display: flex;
    gap: 10px;
    justify-content: center;
    margin-bottom: 30px;
    flex-wrap: wrap;
  }
  
  input {
    padding: 10px 15px;
    border: 1px solid #444;
    border-radius: 4px;
    width: 250px;
    background-color: #1e1e1e;
    color: #e0e0e0;
    font-size: 14px;
  }
  
  input:focus {
    outline: none;
    border-color: #007acc;
  }
  
  button {
    padding: 10px 20px;
    border: none;
    border-radius: 4px;
    cursor: pointer;
    font-weight: 500;
    transition: background-color 0.2s;
  }
  
  button:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }

  button:not(:disabled):hover {
    opacity: 0.9;
  }

  .btn-activo {
    background-color: #d32f2f;
    color: white;
  }

  .btn-inactivo {
    background-color: #388e3c;
    color: white;
  }

  .images-container {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(350px, 1fr));
    gap: 30px;
    margin-bottom: 40px;
  }

  .image-with-controls {
    background-color: #1e1e1e;
    border: 1px solid #333;
    border-radius: 8px;
    padding: 20px;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: space-between;
    min-height: 300px;
  }

  .image-container {
    width: 100%;
    margin-bottom: 20px;
    overflow: hidden;
    border-radius: 4px;
    border: 1px solid #444;
    background-color: #2d2d2d;
    position: relative;
  }

  .image-16-9 {
    aspect-ratio: 16 / 9;
    height: auto;
  }

  .rotatable-image {
    width: 100%;
    height: 100%;
    object-fit: cover;
    transition: transform 0.5s ease;
  }

  .info-container {
    width: 100%;
    padding-top: 10px;
    border-top: 1px solid #333;
  }

  .zoom-info {
    display: flex;
    justify-content: center;
    align-items: center;
    gap: 12px;
    font-size: 14px;
    color: #aaa;
    margin-bottom: 8px;
    flex-wrap: wrap;
  }

  .relacion-info {
    text-align: center;
    font-size: 13px;
    color: #666;
    font-style: italic;
  }

  .separador {
    color: #555;
  }

  .no-detection-message {
    text-align: center;
    padding: 40px 20px;
    color: #888;
    flex-grow: 1;
    display: flex;
    flex-direction: column;
    justify-content: center;
    align-items: center;
  }

  .no-detection-icon {
    font-size: 48px;
    margin-bottom: 10px;
    opacity: 0.5;
  }

  .rango-info, .rango-total {
    font-size: 12px;
    color: #666;
    margin-top: 5px;
  }

  .deteccion-resumen {
    background-color: #1e1e1e;
    border: 1px solid #333;
    border-radius: 8px;
    padding: 20px;
    margin-bottom: 30px;
  }

  .resumen-columnas {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
    gap: 20px;
    margin-top: 20px;
  }

  .columna {
    display: flex;
    flex-direction: column;
    gap: 15px;
  }

  .columna-titulo {
    color: #007acc;
    font-weight: 600;
    padding-bottom: 10px;
    border-bottom: 1px solid #333;
  }

  .objeto-card {
    background-color: #2d2d2d;
    border: 1px solid #444;
    border-radius: 6px;
    padding: 15px;
  }

  .objeto-card.detectado {
    border-left: 4px solid #4caf50;
  }

  .objeto-card.no-detectado {
    border-left: 4px solid #f44336;
    opacity: 0.8;
  }

  .objeto-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 10px;
  }

  .objeto-nombre {
    font-weight: 600;
    color: #ffffff;
  }

  .objeto-estado {
    font-size: 12px;
    padding: 3px 8px;
    border-radius: 12px;
    background-color: #333;
  }

  .objeto-detalles {
    font-size: 14px;
    line-height: 1.5;
  }

  .objeto-detalles p {
    margin: 5px 0;
  }

  .objeto-detalles strong {
    color: #aaa;
    display: inline-block;
    min-width: 100px;
  }

  .id-info {
    font-size: 11px;
    color: #666;
    font-style: italic;
  }

  .total-objetos {
    margin-top: 20px;
    padding-top: 15px;
    border-top: 1px solid #333;
    text-align: center;
    color: #aaa;
  }

  .relaciones {
    background-color: #1e1e1e;
    border: 1px solid #333;
    border-radius: 8px;
    padding: 20px;
    margin-bottom: 30px;
  }

  .relacion-grupo {
    background-color: #2d2d2d;
    border: 1px solid #444;
    border-radius: 6px;
    padding: 15px;
    margin-bottom: 15px;
  }

  .relacion-grupo:last-child {
    margin-bottom: 0;
  }

  .relacion-grupo h4 {
    color: #4caf50;
    margin-top: 0;
    margin-bottom: 10px;
  }

  .relacion-datos {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
    gap: 8px;
  }

  .relacion-datos p {
    margin: 4px 0;
    font-family: 'Consolas', 'Monaco', monospace;
    font-size: 14px;
  }

  .relacion-datos strong {
    color: #aaa;
    min-width: 200px;
    display: inline-block;
  }

  .datos {
    background-color: #1e1e1e;
    border: 1px solid #333;
    border-radius: 8px;
    padding: 20px;
    margin-bottom: 30px;
  }

  pre {
    background-color: #0d1117;
    color: #c9d1d9;
    padding: 15px;
    border-radius: 6px;
    overflow: auto;
    font-family: 'Consolas', 'Monaco', monospace;
    font-size: 13px;
    line-height: 1.5;
    max-height: 400px;
    border: 1px solid #30363d;
  }

  @media (max-width: 768px) {
    .container {
      padding: 15px;
    }
    
    .images-container {
      grid-template-columns: 1fr;
    }
    
    .control-centered {
      flex-direction: column;
      align-items: center;
    }
    
    input {
      width: 100%;
      max-width: 300px;
    }
    
    .resumen-columnas {
      grid-template-columns: 1fr;
    }
    
    .zoom-info {
      flex-direction: column;
      gap: 4px;
    }
    
    .relacion-datos {
      grid-template-columns: 1fr;
    }
    
    .image-16-9 {
      height: 180px;
    }
  }
</style>