--[[
    Sistema Avançado de Replicação para Delta Executor
    
    Este script é uma versão aprimorada e mais poderosa do Replicador.
    Ele captura e reproduz não apenas eventos, mas também mudanças em
    propriedades, criação/destruição de instâncias e muito mais.
    Ele é projetado para replicar com alta precisão eventos complexos
    como os "brainrots".
    
    ATUALIZAÇÃO: As funcionalidades de salvar e carregar arquivos foram
    removidas para evitar o erro de vulnerabilidade que ocorre em alguns
    executores. O script agora é mais seguro e compatível.
]]--

-- Obtém os serviços essenciais do jogo.
local RunService = game:GetService("RunService")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local Players = game:GetService("Players")
local Workspace = game:GetService("Workspace")
local Lighting = game:GetService("Lighting")
local SoundService = game:GetService("SoundService")
local localPlayer = Players.LocalPlayer

-- Define os módulos do script.
local ReplicatorCore = {}
local UniversalCapture = {}
local ReplayEngine = {}
local ControlGUI = {}

--[[
    MÓDULO: ReplicatorCore
    
    Gerencia o estado global do sistema (gravação/reprodução).
]]--
function ReplicatorCore:init()
    self.IsRecording = true
    self.IsReplaying = false
    self.RecordedEvents = {}
end

function ReplicatorCore:startRecording()
    self.IsRecording = true
    self.IsReplaying = false
    print("▶️ Gravação iniciada.")
end

function ReplicatorCore:stopRecording()
    self.IsRecording = false
    print("⏹️ Gravação parada.")
end

function ReplicatorCore:startReplay(events, speed)
    if self.IsReplaying then
        warn("⚠️ Já está em modo de replicação!")
        return
    end
    self.IsRecording = false
    self.IsReplaying = true
    self.EventsToReplay = events or {}
    self.ReplaySpeed = speed or 1.0
    print("▶️ Replicação iniciada.")
end

function ReplicatorCore:stopReplay()
    self.IsReplaying = false
    print("⏹️ Replicação parada.")
end

function ReplicatorCore:clearData()
    self.RecordedEvents = {}
    print("🗑️ Dados de replicação limpos.")
end

ReplicatorCore:init()

--[[
    MÓDULO: UniversalCapture
    
    Responsável por capturar todos os eventos e alterações no jogo.
]]--
UniversalCapture.Filters = {
    -- Filtros para ignorar eventos e propriedades desnecessárias.
    ignore_properties = {
        "Color3", "BrickColor", "Color", "Transparency", "CanCollide",
        "Material", "Reflectance", "SpecularColor", "StudsPerTileU", "StudsPerTileV"
    },
    ignore_remotes = {
        "UpdateCamera", "SetCamera", "MousePosition", "Ping",
        "HeartBeat", "FPS", "Render"
    }
}

-- Captura e armazena um evento.
function UniversalCapture:CaptureEvent(eventType, data)
    if not ReplicatorCore.IsRecording then return end
    
    local event = {
        type = eventType,
        data = data,
        timestamp = tick(),
        player = localPlayer.Name
    }
    
    table.insert(ReplicatorCore.RecordedEvents, event)
    
    print(string.format("📊 [%s] Evento capturado: %s", eventType:upper(), tostring(data.remote or data.name or "Desconhecido")))
end

-- Rastreia e captura a criação e destruição de instâncias.
function UniversalCapture:InstanceTracker()
    local function captureInstanceEvent(instance, eventType)
        local data = {
            name = instance.Name,
            class = instance.ClassName,
            path = instance:GetFullName()
        }
        
        -- Captura propriedades importantes no momento da criação.
        if eventType == "InstanceCreated" then
            data.position = (instance:IsA("BasePart") or instance:IsA("Model")) and tostring(instance.Position) or nil
            data.cframe = (instance:IsA("BasePart") or instance:IsA("Model")) and tostring(instance.CFrame) or nil
        end
        
        self:CaptureEvent(eventType, data)
    end
    
    -- Conecta aos eventos de adição/remoção de descendentes.
    local function setupDescendantListeners(parent)
        parent.DescendantAdded:Connect(function(descendant)
            captureInstanceEvent(descendant, "InstanceCreated")
        end)
        
        parent.DescendantRemoving:Connect(function(descendant)
            captureInstanceEvent(descendant, "InstanceDestroyed")
        end)
    end
    
    setupDescendantListeners(Workspace)
    setupDescendantListeners(ReplicatedStorage)
    setupDescendantListeners(Lighting)
    
    print("📦 Rastreamento de instâncias ativado.")
end

-- Hooka todos os RemoteEvents e RemoteFunctions.
function UniversalCapture:RemoteHooker()
    local hookedRemotes = {}
    
    local function hookRemote(remote)
        if hookedRemotes[remote] then return end
        
        local remoteName = remote.Name
        for _, ignored in ipairs(self.Filters.ignore_remotes) do
            if string.find(string.lower(remoteName), string.lower(ignored)) then
                return
            end
        end
        
        hookedRemotes[remote] = true
        
        if remote:IsA("RemoteEvent") then
            local originalFire = remote.FireServer
            remote.FireServer = function(self, ...)
                local args = {...}
                local captureData = { remote = remoteName, path = remote:GetFullName(), args = args, type = "RemoteEvent" }
                UniversalCapture:CaptureEvent("RemoteFired", captureData)
                return originalFire(self, unpack(args))
            end
        elseif remote:IsA("RemoteFunction") then
            local originalInvoke = remote.InvokeServer
            remote.InvokeServer = function(self, ...)
                local args = {...}
                local captureData = { remote = remoteName, path = remote:GetFullName(), args = args, type = "RemoteFunction" }
                UniversalCapture:CaptureEvent("RemoteInvoked", captureData)
                return originalInvoke(self, unpack(args))
            end
        end
    end
    
    -- Escaneia e hooka remotes existentes.
    local function scanAndHook(container)
        for _, obj in ipairs(container:GetDescendants()) do
            if obj:IsA("RemoteEvent") or obj:IsA("RemoteFunction") then
                hookRemote(obj)
            end
        end
    end
    
    scanAndHook(ReplicatedStorage)
    scanAndHook(Workspace)
    print("🌐 Remotes hookeados.")
end

-- Rastreia mudanças de propriedades do jogador e de sua câmera.
function UniversalCapture:PropertyTracker()
    local lastPlayerState = {}
    local trackedProperties = {"Health", "WalkSpeed", "JumpPower", "DisplayName", "TeamColor"}
    
    local function trackState()
        local player = localPlayer
        local character = player.Character
        
        local state = {
            position = character and character:FindFirstChild("HumanoidRootPart") and character.HumanoidRootPart.Position or Vector3.new(0,0,0),
            cframe = character and character:FindFirstChild("HumanoidRootPart") and character.HumanoidRootPart.CFrame or CFrame.new(),
            cameraCFrame = Workspace.CurrentCamera.CFrame,
            properties = {}
        }
        
        for _, prop in ipairs(trackedProperties) do
            if player[prop] ~= nil and player[prop] ~= lastPlayerState[prop] then
                state.properties[prop] = player[prop]
            end
        end
        
        if lastPlayerState.position then
            local distance = (state.position - lastPlayerState.position).Magnitude
            if distance > 0.1 then
                self:CaptureEvent("Movement", {
                    position = {x = state.position.X, y = state.position.Y, z = state.position.Z},
                    cframe = {x = state.cframe.X, y = state.cframe.Y, z = state.cframe.Z}
                })
            end
        end
        
        if state.cameraCFrame ~= lastPlayerState.cameraCFrame then
             self:CaptureEvent("CameraChange", {
                cframe = {x = state.cameraCFrame.X, y = state.cameraCFrame.Y, z = state.cameraCFrame.Z}
            })
        end
        
        lastPlayerState = state
    end
    
    RunService.Heartbeat:Connect(trackState)
    print("🏃 Propriedades rastreadas.")
end

-- Rastreia sons.
function UniversalCapture:SoundTracker()
    local function trackSound(sound)
        if not sound:IsA("Sound") then return end
        sound.Played:Connect(function()
            self:CaptureEvent("SoundPlayed", {
                name = sound.Name,
                id = sound.SoundId,
                volume = sound.Volume
            })
        end)
    end
    
    for _, obj in ipairs(Workspace:GetDescendants()) do
        trackSound(obj)
    end
    
    Workspace.DescendantAdded:Connect(trackSound)
    print("🔊 Sons rastreados.")
end

--[[
    MÓDULO: ReplayEngine
    
    Responsável por reproduzir os eventos capturados.
]]--
function ReplayEngine:ReplayEvent(event)
    print(string.format("🔄 Replicando [%s]...", event.type))
    
    if event.type == "RemoteFired" then
        local remote = ReplicatedStorage:FindFirstChild(event.data.remote, true) or Workspace:FindFirstChild(event.data.remote, true)
        if remote and remote:IsA("RemoteEvent") then
            remote:FireServer(unpack(event.data.args))
        end
    elseif event.type == "RemoteInvoked" then
        local remote = ReplicatedStorage:FindFirstChild(event.data.remote, true) or Workspace:FindFirstChild(event.data.remote, true)
        if remote and remote:IsA("RemoteFunction") then
            remote:InvokeServer(unpack(event.data.args))
        end
    elseif event.type == "Movement" then
        local character = localPlayer.Character
        if character and character:FindFirstChild("HumanoidRootPart") then
            local pos = event.data.position
            character.HumanoidRootPart.CFrame = CFrame.new(pos.x, pos.y, pos.z)
        end
    elseif event.type == "SoundPlayed" then
        local sound = Instance.new("Sound")
        sound.SoundId = event.data.id
        sound.Volume = event.data.volume
        sound.Parent = Workspace
        sound:Play()
        sound.Ended:Connect(function() sound:Destroy() end)
    elseif event.type == "InstanceCreated" then
        -- Não podemos replicar a criação de instâncias diretamente pois não sabemos o modelo.
        -- Poderíamos usar o nome para tentar encontrar um no ReplicatedStorage e clonar.
        print("⚠️ Replicando criação de instância. Isso pode não funcionar sem o modelo correto.")
    end
end

-- Inicia a reprodução de uma lista de eventos.
function ReplayEngine:startReplay()
    if not ReplicatorCore.IsReplaying then return end
    
    local events = ReplicatorCore.EventsToReplay
    
    -- Ordena os eventos por timestamp.
    table.sort(events, function(a, b) return a.timestamp < b.timestamp end)
    
    local lastTimestamp = events[1].timestamp
    
    for _, event in ipairs(events) do
        if not ReplicatorCore.IsReplaying then break end
        
        local delay = (event.timestamp - lastTimestamp) / ReplicatorCore.ReplaySpeed
        if delay > 0.01 then
            wait(delay)
        end
        
        self:ReplayEvent(event)
        lastTimestamp = event.timestamp
    end
    
    ReplicatorCore:stopReplay()
end

--[[
    MÓDULO: ControlGUI
    
    Cria a interface de usuário completa e interativa.
]]--
function ControlGUI:create()
    local playerGui = localPlayer:WaitForChild("PlayerGui")
    local oldGui = playerGui:FindFirstChild("ReplicatorGUI")
    if oldGui then oldGui:Destroy() end
    
    local screenGui = Instance.new("ScreenGui")
    screenGui.Name = "ReplicatorGUI"
    screenGui.Parent = playerGui
    
    local mainFrame = Instance.new("Frame")
    mainFrame.Size = UDim2.new(0, 350, 0, 450)
    mainFrame.Position = UDim2.new(0, 10, 0, 10)
    mainFrame.BackgroundColor3 = Color3.new(0.05, 0.05, 0.05)
    mainFrame.BorderSizePixel = 2
    mainFrame.BorderColor3 = Color3.new(0, 1, 0)
    mainFrame.Parent = screenGui
    
    local title = Instance.new("TextLabel")
    title.Size = UDim2.new(1, 0, 0, 30)
    title.BackgroundTransparency = 1
    title.Text = "🎬 Replicador Avançado"
    title.TextColor3 = Color3.new(0, 1, 0)
    title.TextSize = 16
    title.Font = Enum.Font.SourceSansBold
    title.Parent = mainFrame
    
    -- Labels de status dinâmicos.
    local statusLabel = Instance.new("TextLabel")
    statusLabel.Size = UDim2.new(1, -10, 0, 20)
    statusLabel.Position = UDim2.new(0, 5, 0, 40)
    statusLabel.BackgroundTransparency = 1
    statusLabel.TextColor3 = Color3.new(0.9, 0.9, 0.9)
    statusLabel.TextSize = 12
    statusLabel.TextXAlignment = Enum.TextXAlignment.Left
    statusLabel.Parent = mainFrame
    self.statusLabel = statusLabel

    local totalLabel = Instance.new("TextLabel")
    totalLabel.Size = UDim2.new(1, -10, 0, 20)
    totalLabel.Position = UDim2.new(0, 5, 0, 65)
    totalLabel.BackgroundTransparency = 1
    totalLabel.TextColor3 = Color3.new(0.9, 0.9, 0.9)
    totalLabel.TextSize = 12
    totalLabel.TextXAlignment = Enum.TextXAlignment.Left
    totalLabel.Parent = mainFrame
    self.totalLabel = totalLabel
    
    local yPos = 100
    
    -- Botões de gravação/parada.
    local recordButton = Instance.new("TextButton")
    recordButton.Size = UDim2.new(1, -10, 0, 30)
    recordButton.Position = UDim2.new(0, 5, 0, yPos)
    recordButton.BackgroundColor3 = Color3.new(0.2, 0.2, 0.2)
    recordButton.Text = "▶️ Iniciar Gravação"
    recordButton.TextColor3 = Color3.new(1, 1, 1)
    recordButton.Parent = mainFrame
    recordButton.MouseButton1Click:Connect(function()
        ReplicatorCore:startRecording()
    end)
    yPos = yPos + 35

    local stopRecordButton = Instance.new("TextButton")
    stopRecordButton.Size = UDim2.new(1, -10, 0, 30)
    stopRecordButton.Position = UDim2.new(0, 5, 0, yPos)
    stopRecordButton.BackgroundColor3 = Color3.new(0.2, 0.2, 0.2)
    stopRecordButton.Text = "⏹️ Parar Gravação"
    stopRecordButton.TextColor3 = Color3.new(1, 1, 1)
    stopRecordButton.Parent = mainFrame
    stopRecordButton.MouseButton1Click:Connect(function()
        ReplicatorCore:stopRecording()
    end)
    yPos = yPos + 40
    
    -- Botões de replicação.
    local replayAllButton = Instance.new("TextButton")
    replayAllButton.Size = UDim2.new(1, -10, 0, 30)
    replayAllButton.Position = UDim2.new(0, 5, 0, yPos)
    replayAllButton.BackgroundColor3 = Color3.new(0.2, 0.2, 0.2)
    replayAllButton.Text = "▶️ Replicar Tudo"
    replayAllButton.TextColor3 = Color3.new(1, 1, 1)
    replayAllButton.Parent = mainFrame
    replayAllButton.MouseButton1Click:Connect(function()
        ReplicatorCore:startReplay(ReplicatorCore.RecordedEvents, 1)
        ReplayEngine:startReplay()
    end)
    yPos = yPos + 35
    
    local stopReplayButton = Instance.new("TextButton")
    stopReplayButton.Size = UDim2.new(1, -10, 0, 30)
    stopReplayButton.Position = UDim2.new(0, 5, 0, yPos)
    stopReplayButton.BackgroundColor3 = Color3.new(0.2, 0.2, 0.2)
    stopReplayButton.Text = "⏸️ Parar Replicação"
    stopReplayButton.TextColor3 = Color3.new(1, 1, 1)
    stopReplayButton.Parent = mainFrame
    stopReplayButton.MouseButton1Click:Connect(function()
        ReplicatorCore:stopReplay()
    end)
    yPos = yPos + 40
    
    -- Botões de dados.
    local clearButton = Instance.new("TextButton")
    clearButton.Size = UDim2.new(1, -10, 0, 30)
    clearButton.Position = UDim2.new(0, 5, 0, yPos)
    clearButton.BackgroundColor3 = Color3.new(0.2, 0.2, 0.2)
    clearButton.Text = "🗑️ Limpar Dados"
    clearButton.TextColor3 = Color3.new(1, 1, 1)
    clearButton.Parent = mainFrame
    clearButton.MouseButton1Click:Connect(function()
        ReplicatorCore:clearData()
    end)
    yPos = yPos + 35
    
    self.gui = screenGui
end

-- Atualiza os labels da GUI.
function ControlGUI:update()
    if not self.statusLabel or not self.totalLabel then return end
    
    local status = "Capturando"
    if ReplicatorCore.IsReplaying then
        status = "Replicando"
    elseif not ReplicatorCore.IsRecording then
        status = "Parado"
    end
    
    self.statusLabel.Text = "▶️ Status: " .. status
    self.totalLabel.Text = "📊 Eventos Capturados: " .. #ReplicatorCore.RecordedEvents
end

--[[
    FUNÇÃO DE INICIALIZAÇÃO PRINCIPAL
]]--
local function Initialize()
    local oldGui = localPlayer.PlayerGui:FindFirstChild("ReplicatorGUI")
    if oldGui then oldGui:Destroy() end

    print("=" .. string.rep("=", 50) .. "=")
    print("🎬 SISTEMA DE REPLICAÇÃO AVANÇADO ATIVO")
    print("=" .. string.rep("=", 50) .. "=")
    
    UniversalCapture:InstanceTracker()
    UniversalCapture:RemoteHooker()
    UniversalCapture:PropertyTracker()
    UniversalCapture:SoundTracker()
    
    ControlGUI:create()
    
    RunService.Heartbeat:Connect(function()
        ControlGUI:update()
    end)
    
    print("✅ Sistema de replicação completo iniciado!")
    print("🎮 Use a interface para controlar a captura e replicação")
end

--[[
    LIMPEZA
]]--
game:BindToClose(function()
    if ControlGUI.gui then
        ControlGUI.gui:Destroy()
    end
    print("👋 Sistema de replicação finalizado!")
end)

Initialize()
