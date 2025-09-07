--[=[
    GUI de Executor para interagir com qualquer objeto do workspace.
    
    Funcionalidades:
    - Lista todos os objetos (Partes, Modelos, etc.) no workspace.
    - Clicar em um objeto o "seleciona" para interações.
    - O objeto selecionado fica disponível na variável global "_G.SelectedObject".
    - Permite executar código no TextBox usando a variável selecionada.
--]=]

local Players = game:GetService("Players")
local LocalPlayer = Players.LocalPlayer
local PlayerGui = LocalPlayer:WaitForChild("PlayerGui")
local Workspace = game:GetService("Workspace")

-- Cria o ScreenGui
local mainGui = Instance.new("ScreenGui")
mainGui.Name = "OmniExecutorGui"
mainGui.ResetOnSpawn = false
mainGui.Parent = PlayerGui

-- Cria o Frame principal
local executorFrame = Instance.new("Frame")
executorFrame.Name = "OmniExecutor"
executorFrame.Size = UDim2.new(0.4, 0, 0.6, 0)
executorFrame.Position = UDim2.new(0.3, 0, 0.2, 0)
executorFrame.BackgroundColor3 = Color3.new(0.1, 0.1, 0.1)
executorFrame.BorderColor3 = Color3.new(0, 0, 0)
executorFrame.BorderSizePixel = 2
executorFrame.Active = true
executorFrame.Draggable = true
executorFrame.Parent = mainGui

-- Cria o Label do título
local titleLabel = Instance.new("TextLabel")
titleLabel.Name = "TitleLabel"
titleLabel.Size = UDim2.new(1, 0, 0.08, 0)
titleLabel.Position = UDim2.new(0, 0, 0, 0)
titleLabel.BackgroundColor3 = Color3.new(0.2, 0.2, 0.2)
titleLabel.TextColor3 = Color3.new(1, 1, 1)
titleLabel.TextScaled = true
titleLabel.Text = "Executor de Objetos do Workspace"
titleLabel.Font = Enum.Font.SourceSansBold
titleLabel.Parent = executorFrame

-- Cria o ScrollingFrame para listar os objetos
local objectsFrame = Instance.new("ScrollingFrame")
objectsFrame.Name = "ObjectsList"
objectsFrame.Size = UDim2.new(0.9, 0, 0.5, 0)
objectsFrame.Position = UDim2.new(0.05, 0, 0.1, 0)
objectsFrame.BackgroundColor3 = Color3.new(0.3, 0.3, 0.3)
objectsFrame.CanvasSize = UDim2.new(0, 0, 0, 0)
objectsFrame.ScrollBarThickness = 8
objectsFrame.Parent = executorFrame

-- Layout para os botões
local listLayout = Instance.new("UIListLayout")
listLayout.Name = "ObjectsListLayout"
listLayout.Padding = UDim.new(0, 5)
listLayout.Parent = objectsFrame

-- Cria o TextBox onde você vai digitar o código
local codeBox = Instance.new("TextBox")
codeBox.Name = "CodeTextBox"
codeBox.Size = UDim2.new(0.9, 0, 0.2, 0)
codeBox.Position = UDim2.new(0.05, 0, 0.65, 0)
codeBox.BackgroundColor3 = Color3.new(0.3, 0.3, 0.3)
codeBox.TextColor3 = Color3.new(1, 1, 1)
codeBox.TextWrapped = true
codeBox.MultiLine = true
codeBox.PlaceholderText = "Use a variável _G.SelectedObject para interagir com o objeto..."
codeBox.Font = Enum.Font.SourceSans
codeBox.Parent = executorFrame

-- Cria o botão de executar
local executeButton = Instance.new("TextButton")
executeButton.Name = "ExecuteButton"
executeButton.Size = UDim2.new(0.4, 0, 0.08, 0)
executeButton.Position = UDim2.new(0.55, 0, 0.88, 0)
executeButton.BackgroundColor3 = Color3.new(0, 0.5, 0)
executeButton.TextColor3 = Color3.new(1, 1, 1)
executeButton.Text = "Executar"
executeButton.Font = Enum.Font.SourceSansBold
executeButton.Parent = executorFrame

-- Cria o botão para fechar a GUI
local closeButton = Instance.new("TextButton")
closeButton.Name = "CloseButton"
closeButton.Size = UDim2.new(0.4, 0, 0.08, 0)
closeButton.Position = UDim2.new(0.05, 0, 0.88, 0)
closeButton.BackgroundColor3 = Color3.new(0.8, 0, 0)
closeButton.TextColor3 = Color3.new(1, 1, 1)
closeButton.Text = "Fechar"
closeButton.Font = Enum.Font.SourceSansBold
closeButton.Parent = executorFrame

-- Variável para guardar o objeto selecionado
_G.SelectedObject = nil

-- Função para atualizar a lista de objetos
local function updateObjectsList()
    -- Limpa a lista atual
    for _, child in pairs(objectsFrame:GetChildren()) do
        if child.Name ~= "ObjectsListLayout" then
            child:Destroy()
        end
    end
    
    local yOffset = 0
    
    for _, child in pairs(Workspace:GetChildren()) do
        local objectButton = Instance.new("TextButton")
        objectButton.Name = child.Name
        objectButton.Size = UDim2.new(1, 0, 0, 30)
        objectButton.BackgroundColor3 = Color3.new(0.5, 0.5, 0.5)
        objectButton.TextColor3 = Color3.new(1, 1, 1)
        objectButton.TextXAlignment = Enum.TextXAlignment.Left
        objectButton.Text = "   " .. child.Name .. " (" .. child.ClassName .. ")"
        objectButton.Font = Enum.Font.SourceSans
        objectButton.Parent = objectsFrame
        
        objectButton.MouseButton1Click:Connect(function()
            _G.SelectedObject = child
            warn("Objeto selecionado:", _G.SelectedObject.Name)
            titleLabel.Text = "Selecionado: " .. _G.SelectedObject.Name
        end)
        
        yOffset = yOffset + 35
    end
    
    objectsFrame.CanvasSize = UDim2.new(0, 0, 0, yOffset)
end

-- Evento para o botão de executar
executeButton.MouseButton1Click:Connect(function()
    local codeToExecute = codeBox.Text
    if _G.SelectedObject then
        local success, result = pcall(function()
            local func = loadstring(codeToExecute)
            if func then
                func()
            end
        end)
        
        if not success then
            warn("Erro de execução:", result)
        end
    else
        warn("Nenhum objeto selecionado. Clique em um objeto na lista primeiro.")
    end
end)

closeButton.MouseButton1Click:Connect(function()
    mainGui:Destroy()
end)

-- Deixa a janela inicialmente invisível
executorFrame.Visible = false

-- Cria o botão no canto da tela
local toggleButton = Instance.new("TextButton")
toggleButton.Name = "ToggleButton"
toggleButton.Size = UDim2.new(0.1, 0, 0.05, 0)
toggleButton.Position = UDim2.new(0.9, 0, 0, 0)
toggleButton.BackgroundColor3 = Color3.new(0.5, 0.5, 0.5)
toggleButton.TextColor3 = Color3.new(1, 1, 1)
toggleButton.Text = "Abrir Executor"
toggleButton.Font = Enum.Font.SourceSansBold
toggleButton.Parent = mainGui

toggleButton.MouseButton1Click:Connect(function()
    executorFrame.Visible = not executorFrame.Visible
    if executorFrame.Visible then
        updateObjectsList()
    end
end)
