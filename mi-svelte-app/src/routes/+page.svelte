<script>
  import { onMount, onDestroy } from 'svelte';

  let ip = "http://192.168.1.94";
  let datos = null;
  let cargando = false;
  let actualizando = false;
  let intervalo;

  // Variables para los cálculos
  let relacionCirculoCuadrado = { distancia: 0, angulo: 0 };
  let relacionTrianguloL = { distancia: 0, angulo: 0 };
  
  // Variables para las rotaciones
  let rotacionImagen1 = 0;
  let rotacionImagen2 = 0;
  
  // Variables para las barras de distancia
  let barraDistanciaCirculoCuadrado = 0;
  let barraDistanciaTrianguloL = 0;

  // Variables para controlar la visibilidad de imágenes
  let mostrarImagenCirculoCuadrado = false;
  let mostrarImagenTrianguloL = false;

  // Mapeo de IDs a nombres
  const nombresObjetos = {
    1: "Círculo",
    2: "Cuadrado", 
    3: "Triángulo",
    4: "L"
  };

  // Función para verificar si un objeto es real
  function esObjetoReal(objeto) {
    return objeto && 
           objeto.detectado === true && 
           objeto.area > 0 && 
           objeto.centro_x > 0 && 
           objeto.centro_y > 0;
  }

  // Función para obtener el nombre del objeto por ID
  function obtenerNombreObjeto(id) {
    return nombresObjetos[id] || `Objeto ${id}`;
  }

  async function obtenerDatos() {
    if (cargando) return;
    
    cargando = true;
    try {
      const response = await fetch(`${ip}/status`);
      datos = await response.json();
      
      if (datos && datos.objetos) {
        const objetos = datos.objetos;
        
        // Verificar objeto 1 (Círculo) y 2 (Cuadrado)
        const circulo = objetos.find(o => o.id === 1);
        const cuadrado = objetos.find(o => o.id === 2);
        
        mostrarImagenCirculoCuadrado = esObjetoReal(circulo) && esObjetoReal(cuadrado);
        
        // Verificar objeto 3 (Triángulo) y 4 (L)
        const triangulo = objetos.find(o => o.id === 3);
        const L = objetos.find(o => o.id === 4);
        
        mostrarImagenTrianguloL = esObjetoReal(triangulo) && esObjetoReal(L);
        
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
    relacionCirculoCuadrado = { distancia: 0, angulo: 0 };
    relacionTrianguloL = { distancia: 0, angulo: 0 };
    rotacionImagen1 = 0;
    rotacionImagen2 = 0;
    barraDistanciaCirculoCuadrado = 0;
    barraDistanciaTrianguloL = 0;
  }

  function calcularRelaciones() {
    const objetos = datos.objetos;
    
    // Calcular relación entre Círculo (1) y Cuadrado (2)
    const circulo = objetos.find(o => o.id === 1);
    const cuadrado = objetos.find(o => o.id === 2);
    
    if (circulo && cuadrado && esObjetoReal(circulo) && esObjetoReal(cuadrado)) {
      relacionCirculoCuadrado.distancia = Math.sqrt(
        Math.pow(cuadrado.centro_x - circulo.centro_x, 2) + 
        Math.pow(cuadrado.centro_y - circulo.centro_y, 2)
      );
      
      relacionCirculoCuadrado.angulo = Math.atan2(
        cuadrado.centro_y - circulo.centro_y,
        cuadrado.centro_x - circulo.centro_x
      ) * (180 / Math.PI);
      
      rotacionImagen1 = relacionCirculoCuadrado.angulo;
      barraDistanciaCirculoCuadrado = Math.min(100, (relacionCirculoCuadrado.distancia / 800) * 100);
    } else {
      relacionCirculoCuadrado = { distancia: 0, angulo: 0 };
      rotacionImagen1 = 0;
      barraDistanciaCirculoCuadrado = 0;
    }

    // Calcular relación entre Triángulo (3) y L (4)
    const triangulo = objetos.find(o => o.id === 3);
    const L = objetos.find(o => o.id === 4);
    
    if (triangulo && L && esObjetoReal(triangulo) && esObjetoReal(L)) {
      relacionTrianguloL.distancia = Math.sqrt(
        Math.pow(L.centro_x - triangulo.centro_x, 2) + 
        Math.pow(L.centro_y - triangulo.centro_y, 2)
      );
      
      relacionTrianguloL.angulo = Math.atan2(
        L.centro_y - triangulo.centro_y,
        L.centro_x - triangulo.centro_x
      ) * (180 / Math.PI);
      
      rotacionImagen2 = relacionTrianguloL.angulo;
      barraDistanciaTrianguloL = Math.min(100, (relacionTrianguloL.distancia / 800) * 100);
    } else {
      relacionTrianguloL = { distancia: 0, angulo: 0 };
      rotacionImagen2 = 0;
      barraDistanciaTrianguloL = 0;
    }
  }

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

  <!-- Imágenes con rotación -->
  <div class="images-container">
    
    <!-- Imagen 1 (Círculo y Cuadrado) -->
    <div class="image-with-controls">
      {#if mostrarImagenCirculoCuadrado}
        <div class="image-container">
          <img 
            src="/imagen1.jpg" 
            alt="Círculo y Cuadrado" 
            style="transform: rotate({rotacionImagen1}deg)"
            class="rotatable-image"
          />
        </div>
        <div class="barra-container">
          <div class="barra-texto">Distancia Círculo-Cuadrado: {barraDistanciaCirculoCuadrado.toFixed(1)}%</div>
          <div class="barra-fondo">
            <div 
              class="barra-progreso" 
              style="width: {barraDistanciaCirculoCuadrado}%"
            ></div>
          </div>
        </div>
      {:else}
        <div class="no-detection-message">
          <div class="no-detection-icon">🔍</div>
          <p>Círculo o Cuadrado no detectados</p>
        </div>
      {/if}
    </div>
    
    <!-- Imagen 2 (Triángulo y L) -->
    <div class="image-with-controls">
      {#if mostrarImagenTrianguloL}
        <div class="image-container">
          <img 
            src="/imagen2.jpg" 
            alt="Triángulo y L" 
            style="transform: rotate({rotacionImagen2}deg)"
            class="rotatable-image"
          />
        </div>
        <div class="barra-container">
          <div class="barra-texto">Distancia Triángulo-L: {barraDistanciaTrianguloL.toFixed(1)}%</div>
          <div class="barra-fondo">
            <div 
              class="barra-progreso" 
              style="width: {barraDistanciaTrianguloL}%"
            ></div>
          </div>
        </div>
      {:else}
        <div class="no-detection-message">
          <div class="no-detection-icon">🔍</div>
          <p>Triángulo o L no detectados</p>
        </div>
      {/if}
    </div>
  </div>

  <!-- Resumen de detecciones -->
  {#if datos && datos.objetos}
    <div class="deteccion-resumen">
      <h3>Estado de Formas Detectadas</h3>
      <div class="resumen-columnas">
        <!-- Columna 1: Círculo y Cuadrado -->
        <div class="columna">
          <div class="columna-titulo">Par 1: Círculo y Cuadrado</div>
          {#each datos.objetos.filter(obj => obj.id === 1 || obj.id === 2) as objeto}
            <div class="objeto-card {objeto.detectado ? 'detectado' : 'no-detectado'}">
              <div class="objeto-header">
                <span class="objeto-nombre">{obtenerNombreObjeto(objeto.id)}</span>
                <span class="objeto-estado">
                  {#if objeto.detectado}
                    ✅ Detectado
                  {:else}
                    ❌ No detectado
                  {/if}
                </span>
              </div>
              {#if objeto.detectado}
                <div class="objeto-detalles">
                  <p><strong>Forma:</strong> {objeto.forma || obtenerNombreObjeto(objeto.id)}</p>
                  <p><strong>Color:</strong> {objeto.color || "N/A"}</p>
                  <p><strong>Área:</strong> {objeto.area > 0 ? objeto.area : "N/A"}</p>
                  <p><strong>Posición:</strong> ({objeto.centro_x}, {objeto.centro_y})</p>
                </div>
              {:else}
                <div class="objeto-detalles">
                  <p><em>Forma no detectada</em></p>
                </div>
              {/if}
            </div>
          {/each}
        </div>
        
        <!-- Columna 2: Triángulo y L -->
        <div class="columna">
          <div class="columna-titulo">Par 2: Triángulo y L</div>
          {#each datos.objetos.filter(obj => obj.id === 3 || obj.id === 4) as objeto}
            <div class="objeto-card {objeto.detectado ? 'detectado' : 'no-detectado'}">
              <div class="objeto-header">
                <span class="objeto-nombre">{obtenerNombreObjeto(objeto.id)}</span>
                <span class="objeto-estado">
                  {#if objeto.detectado}
                    ✅ Detectado
                  {:else}
                    ❌ No detectado
                  {/if}
                </span>
              </div>
              {#if objeto.detectado}
                <div class="objeto-detalles">
                  <p><strong>Forma:</strong> {objeto.forma || obtenerNombreObjeto(objeto.id)}</p>
                  <p><strong>Color:</strong> {objeto.color || "N/A"}</p>
                  <p><strong>Área:</strong> {objeto.area > 0 ? objeto.area : "N/A"}</p>
                  <p><strong>Posición:</strong> ({objeto.centro_x}, {objeto.centro_y})</p>
                </div>
              {:else}
                <div class="objeto-detalles">
                  <p><em>Forma no detectada</em></p>
                </div>
              {/if}
            </div>
          {/each}
        </div>
      </div>
      <div class="total-objetos">
        <p>Formas detectadas: <strong>{datos.total_objetos || 0}</strong> de {datos.max_posibles || 4}</p>
      </div>
    </div>
  {/if}

  <!-- Relaciones calculadas -->
  {#if datos && datos.objetos && (mostrarImagenCirculoCuadrado || mostrarImagenTrianguloL)}
    <div class="relaciones">
      <h3>Relaciones Entre Formas</h3>
      
      {#if mostrarImagenCirculoCuadrado}
        <div class="relacion-grupo">
          <h4>Círculo ↔ Cuadrado:</h4>
          <p>Distancia: {relacionCirculoCuadrado.distancia.toFixed(2)} px</p>
          <p>Ángulo: {relacionCirculoCuadrado.angulo.toFixed(2)}°</p>
          <p>Rotación Imagen 1: {rotacionImagen1.toFixed(2)}°</p>
        </div>
      {/if}
      
      {#if mostrarImagenTrianguloL}
        <div class="relacion-grupo">
          <h4>Triángulo ↔ L:</h4>
          <p>Distancia: {relacionTrianguloL.distancia.toFixed(2)} px</p>
          <p>Ángulo: {relacionTrianguloL.angulo.toFixed(2)}°</p>
          <p>Rotación Imagen 2: {rotacionImagen2.toFixed(2)}°</p>
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
    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
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
    min-height: 350px;
  }

  .image-container {
    width: 200px;
    height: 200px;
    display: flex;
    justify-content: center;
    align-items: center;
    margin-bottom: 20px;
    overflow: hidden;
    border-radius: 4px;
    border: 1px solid #444;
  }

  .rotatable-image {
    width: 100%;
    height: 100%;
    object-fit: cover;
    transition: transform 0.3s ease;
  }

  .no-detection-message {
    text-align: center;
    padding: 60px 20px;
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

  .barra-container {
    width: 100%;
    margin-top: auto;
    padding-top: 15px;
    border-top: 1px solid #333;
  }

  .barra-texto {
    font-size: 14px;
    color: #aaa;
    margin-bottom: 8px;
    text-align: center;
  }

  .barra-fondo {
    width: 100%;
    height: 20px;
    background-color: #2d2d2d;
    border-radius: 10px;
    overflow: hidden;
  }

  .barra-progreso {
    height: 100%;
    background-color: #4caf50;
    transition: width 0.3s;
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
    min-width: 60px;
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

  .relacion-grupo p {
    margin: 5px 0;
    font-family: 'Consolas', 'Monaco', monospace;
    font-size: 14px;
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
    
    .image-container {
      width: 180px;
      height: 180px;
    }
  }
</style>