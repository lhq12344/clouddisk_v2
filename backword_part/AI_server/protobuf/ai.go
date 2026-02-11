package aipb

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"time"

	"github.com/mark3labs/mcp-go/client"
	mcptransport "github.com/mark3labs/mcp-go/client/transport"
	"github.com/mark3labs/mcp-go/mcp"
)

type AIServer struct {
	UnimplementedAIServiceServer
}

// ---------- DashScope(OpenAI-compatible) Chat Completions ----------

type ChatCompletionRequest struct {
	Model    string    `json:"model"`
	Messages []Message `json:"messages"`
	Stream   bool      `json:"stream"`
	Tools    []LLMTool `json:"tools,omitempty"`
	// ToolChoice any `json:"tool_choice,omitempty"` // 如需要可加: "auto" / {"type":"function","function":{"name":"xxx"}}
}

type Message struct {
	Role       string        `json:"role"`
	Content    string        `json:"content,omitempty"`
	ToolCalls  []LLMToolCall `json:"tool_calls,omitempty"`
	ToolCallID string        `json:"tool_call_id,omitempty"`
}

type ChatCompletionResponse struct {
	ID      string    `json:"id,omitempty"`
	Object  string    `json:"object,omitempty"`
	Created int64     `json:"created,omitempty"`
	Model   string    `json:"model,omitempty"`
	Choices []Choice  `json:"choices"`
	Error   *LLMError `json:"error,omitempty"`
}

type Choice struct {
	Index        int     `json:"index"`
	Message      Message `json:"message"`
	FinishReason string  `json:"finish_reason,omitempty"`
}

type LLMError struct {
	Message string `json:"message"`
	Type    string `json:"type,omitempty"`
	Code    any    `json:"code,omitempty"`
}

type LLMTool struct {
	Type     string          `json:"type"` // "function"
	Function LLMToolFunction `json:"function"`
}

type LLMToolFunction struct {
	Name        string          `json:"name"`
	Description string          `json:"description,omitempty"`
	Parameters  json.RawMessage `json:"parameters,omitempty"`
}

type LLMToolCall struct {
	ID       string              `json:"id,omitempty"`
	Type     string              `json:"type"` // "function"
	Function LLMToolCallFunction `json:"function"`
}

type LLMToolCallFunction struct {
	Name      string `json:"name"`
	Arguments string `json:"arguments,omitempty"` // OpenAI-compatible: arguments 是 JSON string
}

// ---------- Config ----------

const (
	// Beijing
	DefaultDashScopeBaseURL = "https://dashscope.aliyuncs.com/compatible-mode/v1"
	DefaultDashScopeModel   = "qwen-plus"

	DefaultMCPSSEURL  = "http://192.168.149.128:18090/mcp/sse"
	MaxToolIterations = 6

	LLMTimeout = 60 * time.Second
	MCPTimeout = 10 * time.Second
)

func (A AIServer) AIrequest(ctx context.Context, req *AIReq) (*AIResp, error) {
	prompt := buildPrompt(req)

	mcpClient, toolDefs, instructions, err := initMCP(ctx, req)
	if err != nil {
		return &AIResp{Code: 503, Message: fmt.Sprintf("Failed to connect to MCP server: %v", err)}, nil
	}
	defer func() { _ = mcpClient.Close() }()

	messages := []Message{
		{Role: "system", Content: buildSystemPrompt(instructions)},
		{Role: "user", Content: prompt},
	}

	httpClient := &http.Client{Timeout: LLMTimeout}

	for i := 0; i < MaxToolIterations; i++ {
		assistantMsg, err := callDashScope(ctx, httpClient, messages, toolDefs)
		if err != nil {
			return &AIResp{Code: 503, Message: fmt.Sprintf("Failed to call LLM: %v", err)}, nil
		}

		// 没有 tool_calls => 直接返回自然语言
		if len(assistantMsg.ToolCalls) == 0 {
			return &AIResp{Code: 200, Message: "Success", Data: assistantMsg.Content}, nil
		}

		// 追加 assistant tool_call 消息
		messages = append(messages, *assistantMsg)

		// 执行工具调用，并把工具输出作为 role=tool 的 message 追加回去
		toolMessages, err := runToolCalls(ctx, mcpClient, assistantMsg.ToolCalls)
		if err != nil {
			return &AIResp{Code: 500, Message: fmt.Sprintf("MCP tool call failed: %v", err)}, nil
		}
		messages = append(messages, toolMessages...)
	}

	return &AIResp{Code: 500, Message: "Tool call limit reached without a final response"}, nil
}

func (A AIServer) mustEmbedUnimplementedAIServiceServer() {}

func buildPrompt(req *AIReq) string {
	if req == nil {
		return "No request payload provided."
	}
	lines := []string{
		fmt.Sprintf("User: %s (ID: %s)", req.Username, req.Userid),
	}
	if req.Query != "" {
		lines = append(lines, "User query:\n"+req.Query)
	}
	// 注意：你完全可以不传 filehash/size，让模型通过 tools 获取
	if req.Filename != "" {
		lines = append(lines, fmt.Sprintf("Filename (optional context): %s", req.Filename))
	}
	lines = append(lines, "Use available tools when you need account or file data.")
	return strings.Join(lines, "\n")
}

func buildSystemPrompt(instructions string) string {
	base := "You are a helpful assistant for a cloud disk system. Use tools when needed and return a concise response."
	if strings.TrimSpace(instructions) == "" {
		return base
	}
	return base + "\n" + instructions
}

// ---------- DashScope call (OpenAI-compatible) ----------

func callDashScope(ctx context.Context, httpClient *http.Client, messages []Message, tools []LLMTool) (*Message, error) {
	apiKey := strings.TrimSpace(os.Getenv("DASHSCOPE_API_KEY"))
	if apiKey == "" {
		return nil, fmt.Errorf("DASHSCOPE_API_KEY is empty")
	}

	baseURL := strings.TrimSpace(os.Getenv("DASHSCOPE_BASE_URL"))
	if baseURL == "" {
		baseURL = DefaultDashScopeBaseURL
	}

	model := strings.TrimSpace(os.Getenv("DASHSCOPE_MODEL"))
	if model == "" {
		model = DefaultDashScopeModel
	}

	reqBody := ChatCompletionRequest{
		Model:    model,
		Messages: messages,
		Stream:   false,
		Tools:    tools,
	}

	bs, err := json.Marshal(reqBody)
	if err != nil {
		return nil, fmt.Errorf("marshal request: %w", err)
	}

	url := strings.TrimRight(baseURL, "/") + "/chat/completions"
	req, err := http.NewRequestWithContext(ctx, "POST", url, bytes.NewBuffer(bs))
	if err != nil {
		return nil, fmt.Errorf("create request: %w", err)
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Authorization", "Bearer "+apiKey)

	resp, err := httpClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("connect to LLM at %s: %w", baseURL, err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		body, _ := io.ReadAll(resp.Body)
		return nil, fmt.Errorf("LLM returned %d: %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}

	var out ChatCompletionResponse
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		return nil, fmt.Errorf("decode response: %w", err)
	}
	if out.Error != nil {
		return nil, fmt.Errorf("LLM error: %s", out.Error.Message)
	}
	if len(out.Choices) == 0 {
		return nil, fmt.Errorf("LLM returned no choices")
	}
	return &out.Choices[0].Message, nil
}

// ---------- MCP init & tools bridge ----------

func initMCP(ctx context.Context, req *AIReq) (*client.Client, []LLMTool, string, error) {
	baseURL := mcpSSEURL()
	if strings.TrimSpace(baseURL) == "" {
		return nil, nil, "", fmt.Errorf("MCP_SSE_URL is empty")
	}

	headers := map[string]string{
		"X-User-Id":  strings.TrimSpace(req.Userid),
		"X-Username": strings.TrimSpace(req.Username),
	}

	mcpClient, err := client.NewSSEMCPClient(baseURL, mcptransport.WithHeaders(headers))
	if err != nil {
		return nil, nil, "", fmt.Errorf("create MCP client: %w", err)
	}

	// 1) 关键修复：Start 用“长寿命 ctx”，不要 WithTimeout，也不要 defer cancel
	//    这样 SSE session 不会被你自己提前取消
	if err := mcpClient.Start(context.Background()); err != nil {
		_ = mcpClient.Close()
		return nil, nil, "", fmt.Errorf("start MCP client: %w", err)
	}

	// 2) Initialize / ListTools 这类“RPC”才用短超时
	rpcCtx, rpcCancel := context.WithTimeout(ctx, MCPTimeout)
	defer rpcCancel()

	initRes, err := mcpClient.Initialize(rpcCtx, mcp.InitializeRequest{
		Params: mcp.InitializeParams{
			ProtocolVersion: mcp.LATEST_PROTOCOL_VERSION,
			ClientInfo:      mcp.Implementation{Name: "clouddisk-aiserver", Version: "1.0.0"},
		},
	})
	if err != nil {
		_ = mcpClient.Close()
		return nil, nil, "", fmt.Errorf("initialize MCP client: %w", err)
	}

	toolsResp, err := mcpClient.ListTools(rpcCtx, mcp.ListToolsRequest{})
	if err != nil {
		_ = mcpClient.Close()
		return nil, nil, "", fmt.Errorf("list MCP tools: %w", err)
	}

	toolDefs, err := buildLLMTools(toolsResp.Tools)
	if err != nil {
		_ = mcpClient.Close()
		return nil, nil, "", fmt.Errorf("convert MCP tools: %w", err)
	}

	return mcpClient, toolDefs, initRes.Instructions, nil
}

func buildLLMTools(tools []mcp.Tool) ([]LLMTool, error) {
	if len(tools) == 0 {
		return nil, nil
	}
	result := make([]LLMTool, 0, len(tools))
	for _, tool := range tools {
		schema, err := toolInputSchemaJSON(tool)
		if err != nil {
			return nil, fmt.Errorf("tool %s schema: %w", tool.Name, err)
		}
		result = append(result, LLMTool{
			Type: "function",
			Function: LLMToolFunction{
				Name:        tool.Name,
				Description: tool.Description,
				Parameters:  schema,
			},
		})
	}
	return result, nil
}

func toolInputSchemaJSON(tool mcp.Tool) (json.RawMessage, error) {
	if len(tool.RawInputSchema) > 0 {
		return tool.RawInputSchema, nil
	}
	if tool.InputSchema.Type == "" && len(tool.InputSchema.Properties) == 0 {
		return nil, nil
	}
	data, err := json.Marshal(tool.InputSchema)
	if err != nil {
		return nil, err
	}
	return data, nil
}

func runToolCalls(ctx context.Context, mcpClient *client.Client, calls []LLMToolCall) ([]Message, error) {
	if len(calls) == 0 {
		return nil, nil
	}

	results := make([]Message, 0, len(calls))
	for _, call := range calls {
		argsAny, err := decodeToolArguments(call.Function.Arguments)
		if err != nil {
			return nil, fmt.Errorf("parse tool arguments for %s: %w", call.Function.Name, err)
		}
		args, ok := argsAny.(map[string]any)
		if !ok {
			return nil, fmt.Errorf("tool %s arguments must be an object, got %T", call.Function.Name, argsAny)
		}

		toolCtx, cancel := context.WithTimeout(ctx, MCPTimeout)
		result, err := mcpClient.CallTool(toolCtx, mcp.CallToolRequest{
			Params: mcp.CallToolParams{
				Name:      call.Function.Name,
				Arguments: args,
			},
		})
		cancel()
		if err != nil {
			return nil, fmt.Errorf("call tool %s: %w", call.Function.Name, err)
		}

		results = append(results, Message{
			Role:       "tool",
			Content:    toolResultText(result),
			ToolCallID: call.ID,
		})
	}
	return results, nil
}

func decodeToolArguments(raw string) (any, error) {
	if strings.TrimSpace(raw) == "" {
		return map[string]any{}, nil
	}

	var decoded any
	if err := json.Unmarshal([]byte(raw), &decoded); err != nil {
		return nil, err
	}

	// 有些模型可能把 arguments 再包一层 string，这里做一次兼容
	if str, ok := decoded.(string); ok {
		str = strings.TrimSpace(str)
		if str == "" {
			return map[string]any{}, nil
		}
		var nested any
		if err := json.Unmarshal([]byte(str), &nested); err == nil {
			return nested, nil
		}
		return str, nil
	}

	return decoded, nil
}

func toolResultText(result *mcp.CallToolResult) string {
	if result == nil {
		return ""
	}
	if result.StructuredContent != nil {
		if data, err := json.Marshal(result.StructuredContent); err == nil {
			return string(data)
		}
	}
	// 兼容 TextContent
	parts := make([]string, 0, len(result.Content))
	for _, content := range result.Content {
		switch typed := content.(type) {
		case mcp.TextContent:
			parts = append(parts, typed.Text)
		case *mcp.TextContent:
			parts = append(parts, typed.Text)
		default:
			raw, err := json.Marshal(content)
			if err == nil {
				parts = append(parts, string(raw))
			}
		}
	}
	if len(parts) > 0 {
		return strings.Join(parts, "\n")
	}
	raw, err := json.Marshal(result)
	if err != nil {
		return fmt.Sprintf("tool result error: %v", err)
	}
	return string(raw)
}

func mcpSSEURL() string {
	if value := strings.TrimSpace(os.Getenv("MCP_SSE_URL")); value != "" {
		return value
	}
	if value := strings.TrimSpace(os.Getenv("MCP_BASE_URL")); value != "" {
		return strings.TrimRight(value, "/") + "/sse"
	}
	return DefaultMCPSSEURL
}
