<script>
  import { onMount, onDestroy } from 'svelte';

  let ip = "http://192.168.1.94";
  let datos = null;
  let cargando = false;
  let actualizando = false;
  let intervalo;

  // Variables para los cálculos
  let relacion12 = { distancia: 0, angulo: 0 };
  let relacion34 = { distancia: 0, angulo: 0 };
  
  // Variables para las rotaciones
  let rotacionImagen1 = 0;
  let rotacionImagen2 = 0;
  
  // Variables para las barras de distancia (normalizadas 0-100)
  let barraDistancia12 = 0;
  let barraDistancia34 = 0;

  async function obtenerDatos() {
    if (cargando) return;
    
    cargando = true;
    try {
      const response = await fetch(`${ip}/status`);
      datos = await response.json();
      console.log("Datos recibidos:", datos);
      
      // Calcular relaciones cuando hay datos
      if (datos && datos.objetos) {
        calcularRelaciones();
      }
    } catch (error) {
      console.error("Error:", error);
    }
    cargando = false;
  }

  function calcularRelaciones() {
    const objetos = datos.objetos;
    
    // Calcular relación entre objetos 1 y 2
    const obj1 = objetos.find(o => o.id === 1);
    const obj2 = objetos.find(o => o.id === 2);
    
    if (obj1 && obj2) {
      relacion12.distancia = Math.sqrt(
        Math.pow(obj2.centro_x - obj1.centro_x, 2) + 
        Math.pow(obj2.centro_y - obj1.centro_y, 2)
      );
      
      relacion12.angulo = Math.atan2(
        obj2.centro_y - obj1.centro_y,
        obj2.centro_x - obj1.centro_x
      ) * (180 / Math.PI);
      
      // Aplicar rotación a imagen 1 (usando el ángulo entre 1 y 2)
      rotacionImagen1 = relacion12.angulo;
      
      // Normalizar distancia para barra (0-100)
      // Máxima distancia posible en área 640x480 = √(640² + 480²) ≈ 800
      barraDistancia12 = Math.min(100, (relacion12.distancia / 800) * 100);
    }

    // Calcular relación entre objetos 3 y 4
    const obj3 = objetos.find(o => o.id === 3);
    const obj4 = objetos.find(o => o.id === 4);
    
    if (obj3 && obj4) {
      relacion34.distancia = Math.sqrt(
        Math.pow(obj4.centro_x - obj3.centro_x, 2) + 
        Math.pow(obj4.centro_y - obj3.centro_y, 2)
      );
      
      relacion34.angulo = Math.atan2(
        obj4.centro_y - obj3.centro_y,
        obj4.centro_x - obj3.centro_x
      ) * (180 / Math.PI);
      
      // Aplicar rotación a imagen 2 (usando el ángulo entre 3 y 4)
      rotacionImagen2 = relacion34.angulo;
      
      // Normalizar distancia para barra (0-100)
      barraDistancia34 = Math.min(100, (relacion34.distancia / 800) * 100);
    }
  }

  function iniciarActualizacionAutomatica() {
    if (intervalo) {
      clearInterval(intervalo);
    }
    
    actualizando = true;
    // Obtener datos inmediatamente
    obtenerDatos();
    // Y luego cada segundo
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
    if (actualizando) {
      detenerActualizacionAutomatica();
    } else {
      iniciarActualizacionAutomatica();
    }
  }

  // Iniciar automáticamente al cargar el componente
  onMount(() => {
    iniciarActualizacionAutomatica();
  });

  // Limpiar intervalo al destruir el componente
  onDestroy(() => {
    detenerActualizacionAutomatica();
  });
</script>

<div class="container">
  <h1>ESP32 Status</h1>
  
  <!-- Controles centrados -->
  <div class="control-centered">
    <input 
      type="text" 
      bind:value={ip} 
      placeholder="IP del ESP32"
      id="ip-input"
    />
    <button on:click={obtenerDatos} disabled={cargando}>
      {cargando ? "🔄 Cargando..." : "📡 Obtener Datos"}
    </button>
    <button on:click={toggleActualizacionAutomatica} class={actualizando ? 'btn-activo' : 'btn-inactivo'}>
      {actualizando ? '⏸️ Pausar Auto' : '▶️ Iniciar Auto'}
    </button>
    
  </div>
  

  <!-- Imágenes con rotación -->
  <div class="images-container">
    
    <div class="image-with-controls">
        
        
      <div class="image-row">
        
        <img 
          src="/imagen1.jpg" 
          alt="Imagen 1" 
          style="transform: rotate({rotacionImagen1}deg)"
          class="rotatable-image"
        />
      </div>
 
      

    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
      
      
      <div class="barra-container">
        <div class="barra-texto">Distancia 1-2: {barraDistancia12.toFixed(1)}%</div>
        <div class="barra-fondo">
          <div 
            class="barra-progreso" 
            style="width: {barraDistancia12}%"
            role="progressbar"
            aria-valuenow={barraDistancia12}
            aria-valuemin="0"
            aria-valuemax="100"
          ></div>
          
        </div>
      </div>
    </div>
    
    <div class="image-with-controls">
      <div class="image-row">
        <img 
          src="/imagen2.jpg" 
          alt="Imagen 2" 
          style="transform: rotate({rotacionImagen2}deg)"
          class="rotatable-image"
          
        />
      </div>
      <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
    <div></div>
      
      <div class="barra-container">
        <div class="barra-texto">Distancia 3-4: {barraDistancia34.toFixed(1)}%</div>
        <div class="barra-fondo">
          <div 
            class="barra-progreso" 
            style="width: {barraDistancia34}%"
            role="progressbar"
            aria-valuenow={barraDistancia34}
            aria-valuemin="0"
            aria-valuemax="100"
          ></div>
        </div>
      </div>
    </div>
  </div>

  <!-- Relaciones calculadas -->
  {#if datos && datos.objetos}
    <div class="relaciones">
      <h3>Relaciones entre Objetos</h3>
      
      <div class="relacion-grupo">
        <h4>Objetos [1,2]:</h4>
        <p>Distancia: {relacion12.distancia.toFixed(2)} px</p>
        <p>Ángulo: {relacion12.angulo.toFixed(2)}°</p>
        <p>Rotación Imagen 1: {rotacionImagen1.toFixed(2)}°</p>
      </div>
      
      <div class="relacion-grupo">
        <h4>Objetos [3,4]:</h4>
        <p>Distancia: {relacion34.distancia.toFixed(2)} px</p>
        <p>Ángulo: {relacion34.angulo.toFixed(2)}°</p>
        <p>Rotación Imagen 2: {rotacionImagen2.toFixed(2)}°</p>
      </div>
    </div>
  {/if}

  <!--luego esto se saca antes de la demo, para franco y tomas-->
  <!-- Datos JSON -->
  {#if datos}
    <div class="datos">
      <h3>Datos recibidos (ver consola)</h3>
      <pre>{JSON.stringify(datos, null, 2)}</pre>
    </div>
  {/if}
</div>

<style>
  .container {
    padding: 20px;
    font-family: Arial, sans-serif;
    text-align: center;
  }

  .control-centered {
    margin: 20px 0;
    display: flex;
    gap: 10px;
    justify-content: center;
    align-items: center;
    flex-wrap: wrap;
  }
  
  input {
    padding: 8px 12px;
    border: 1px solid #ccc;
    border-radius: 4px;
    width: 200px;
  }
  
  button {
    padding: 8px 16px;
    color: white;
    border: none;
    border-radius: 4px;
    cursor: pointer;
    transition: background-color 0.3s;
  }
  
  button:disabled {
    background: #ccc;
  }

  .btn-activo {
    background: #e74c3c;
  }

  .btn-activo:hover {
    background: #c0392b;
  }

  .btn-inactivo {
    background: #2ecc71;
  }

  .btn-inactivo:hover {
    background: #27ae60;
  }

  .images-container {
    display: flex;
    justify-content: center;
    gap: 40px;
    flex-wrap: wrap;
    margin: 20px 0;
  }

  .image-with-controls {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 15px;
  }

  .image-row {
    display: flex;
    justify-content: center;
    align-items: center;
  }

  .rotatable-image {
    width: 50%;
    max-width: 540px;
    height: auto;
    transition: transform 0.3s ease;
    border: 2px solid #007acc;
    border-radius: 8px;
  }

  .barra-container {
    width: 100%;
    max-width: 300px;
    text-align: center;
  }

  .barra-texto {
    display: block;
    margin-bottom: 5px;
    font-weight: bold;
    color: #333;
  }

  .barra-fondo {
    width: 100%;
    height: 20px;
    background-color: #f0f0f0;
    border-radius: 10px;
    overflow: hidden;
    border: 1px solid #ccc;
  }

  .barra-progreso {
    height: 100%;
    background: linear-gradient(90deg, #4CAF50, #8BC34A);
    transition: width 0.3s ease;
    border-radius: 10px;
  }
  
  .relaciones {
    background: #f0f8ff;
    padding: 15px;
    border-radius: 4px;
    margin: 20px 0;
    border: 1px solid #007acc;
  }

  .relacion-grupo {
    margin: 15px 0;
    padding: 10px;
    background: white;
    border-radius: 4px;
  }

  .relacion-grupo h4 {
    margin: 0 0 10px 0;
    color: #007acc;
  }

  .relacion-grupo p {
    margin: 5px 0;
    font-family: 'Courier New', monospace;
  }
  
  .datos {
    background: #f5f5f5;
    padding: 15px;
    border-radius: 4px;
    text-align: left;
  }
  
  pre {
    white-space: pre-wrap;
    word-wrap: break-word;
  }

  /* Responsive */
  @media (max-width: 768px) {
    .images-container {
      flex-direction: column;
      gap: 20px;
    }
    
    .control-centered {
      flex-direction: column;
    }
    
    input {
      width: 100%;
      max-width: 300px;
    }
  }
</style>