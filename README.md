# ESP32-CAM Viewer - Aplicación de Escritorio

## 📝 Descripción
Aplicación de escritorio desarrollada con **Svelte + Tauri** para visualizar el stream de video de una cámara ESP32-CAM. Permite ver video en tiempo real, tomar capturas y controlar diferentes formatos de visualización.

## 👨‍💻 Autor
**Pedro Giacoia**

## 🛠️ Requisitos del Sistema

### Software Requerido
1. **Node.js** (versión 18 o superior)
   - Descargar desde: [nodejs.org](https://nodejs.org/)
2. **Rust** 
   - Instalar desde: [rustup.rs](https://rustup.rs/)
3. **Visual Studio Build Tools** (Windows)
   - Incluir: "Desktop development with C++"

### Verificar Instalaciones
```bash
node --version
npm --version
rustc --version
cargo --version
```

## 🚀 Instalación y Configuración

### 1. Instalar Dependencias de Node.js
```bash
npm install --legacy-peer-deps
```

### 2. Instalar Dependencias de Tauri
```bash
npm install @tauri-apps/cli @tauri-apps/api --save-dev
```

### 3. Inicializar Tauri (si es necesario)
```bash
npx tauri init
```

**Durante la inicialización, usar:**
- Nombre de la app: `TDPII-SOFTWARE`
- Título de ventana: `TDPII-SOFTWARE`
- Assets web: `../dist`
- URL desarrollo: `http://localhost:5173`
- Comando desarrollo: `npm run dev`
- Comando build: `npm run build`

## 🎯 Ejecutar la Aplicación

### Modo Desarrollo
```bash
npm run tauri:dev
```

### Compilar para Producción
```bash
npm run tauri:build
```

## 📡 Configuración ESP32-CAM

### Código Arduino Requerido
El proyecto asume que tienes programada una ESP32-CAM con un servidor web que provee:
- Stream MJPEG en: `/stream`
- Capturas estáticas en: `/capture`
- Página web básica en: `/`

### Configurar IP de la ESP32
1. Ejecuta la aplicación
2. En la interfaz, ingresa la IP de tu ESP32-CAM
3. Selecciona el formato de visualización deseado

## 🗂️ Estructura del Proyecto
```
esp-viewer/
├── src/                 # Código fuente Svelte
│   ├── App.svelte      # Componente principal
│   └── main.js         # Punto de entrada
├── src-tauri/          # Código Rust (Tauri)
│   ├── src/
│   ├── Cargo.toml
│   └── tauri.conf.json
├── package.json        # Dependencias Node.js
└── vite.config.mjs     # Configuración Vite
```

## ⚙️ Scripts Disponibles

| Comando | Descripción |
|---------|-------------|
| `npm run dev` | Servidor desarrollo Vite |
| `npm run build` | Compilar frontend |
| `npm run tauri:dev` | Ejecutar aplicación Tauri en desarrollo |
| `npm run tauri:build` | Compilar aplicación de escritorio |

## 🔧 Solución de Problemas

### Error: "Cannot find module"
```bash
npm cache clean --force
rm -r node_modules
rm package-lock.json
npm install --legacy-peer-deps
```

### Error: "Tauri project not recognized"
- Verificar que `tauri.conf.json` exista en `src-tauri/`
- Ejecutar desde el directorio raíz del proyecto

### Ventana en Blanco
- Verificar que los archivos Svelte estén en `src/`
- Ejecutar `npm run dev` primero para compilar el frontend
