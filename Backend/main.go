package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"strconv"
	"sync"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	//"github.com/rs/cors"
)

/*
Data Models
*/
type SlotStatus struct {
	SlotID    int       `json:"slot_id"`
	Status    string    `json:"status"` // "available", "occupied", "unknown"
	UpdatedAt time.Time `json:"updated_at"`
}

/*
Thread-safe store for parking slot statuses
*/
type ParkingStore struct {
	mu    sync.RWMutex
	slots map[int]*SlotStatus
}

func NewParkingStore() *ParkingStore {
	store := &ParkingStore{
		slots: make(map[int]*SlotStatus),
	}

	// Initialize all 4 slots as unknown
	for i := 1; i <= 4; i++ {
		store.slots[i] = &SlotStatus{
			SlotID:    i,
			Status:    "unknown",
			UpdatedAt: time.Now(),
		}
	}
	return store
}

func (ps *ParkingStore) Update(slotID int, status string) {
	ps.mu.Lock()
	defer ps.mu.Unlock()
	ps.slots[slotID] = &SlotStatus{
		SlotID:    slotID,
		Status:    status,
		UpdatedAt: time.Now(),
	}
}

func (ps *ParkingStore) GetAll() []SlotStatus {
	ps.mu.RLock()
	defer ps.mu.RUnlock()
	result := make([]SlotStatus, 0, 4)
	for i := 1; i <= 4; i++ {
		result = append(result, *ps.slots[i])
	}
	return result
}

/*
MQTT Configuration
*/
const (
	mqttBroker = "tcp://192.168.100.210:1883"
	mqttClient = "parking-go-backend"
)

/*
CORS Middleware
*/
func corsMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")

		// Handle preflight requests
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusOK)
			return
		}

		next.ServeHTTP(w, r)
	})
}

/*
Main
*/
func main() {
	store := NewParkingStore()

	// --- MQTT Client Setup ---
	opts := mqtt.NewClientOptions().
		AddBroker(mqttBroker).
		SetClientID(mqttClient).
		SetAutoReconnect(true).
		SetConnectRetryInterval(5 * time.Second).
		SetOnConnectHandler(func(c mqtt.Client) {
			log.Println("✅ Connected to MQTT broker")
			// Subscribe to all 4 parking slot topics
			for i := 1; i <= 4; i++ {
				topic := fmt.Sprintf("parking/slot/%d", i)
				token := c.Subscribe(topic, 1, func(client mqtt.Client, msg mqtt.Message) {
					handleMessage(store, msg)
				})
				token.Wait()
				if token.Error() != nil {
					log.Printf("❌ Subscribe error on %s: %v", topic, token.Error())
				} else {
					log.Printf("📡 Subscribed to topic: %s", topic)
				}
			}
		}).
		SetConnectionLostHandler(func(c mqtt.Client, err error) {
			log.Printf("⚠️ Connection lost: %v", err)
		})

	client := mqtt.NewClient(opts)
	token := client.Connect()
	token.Wait()
	if token.Error() != nil {
		log.Fatalf("❌ Failed to connect to MQTT broker: %v", token.Error())
	}

	// --- REST API ---
	mux := http.NewServeMux()

	// GET /api/parking — returns all 4 slot statuses
	mux.HandleFunc("/api/parking", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(store.GetAll())
	})

	// Health check
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		w.Write([]byte(`{"status":"ok"}`))
	})

	// // Wrap with CORS middleware (allows React dev server on :3000)
	// handler := cors.New(cors.Options{
	// 	AllowedOrigins:   []string{"http://localhost:3000"},
	// 	AllowedMethods:   []string{"GET"},
	// 	AllowedHeaders:   []string{"Content-Type"},
	// 	AllowCredentials: true,
	// }).Handler(mux)

	handler := corsMiddleware(mux)

	log.Println("Parking API server running on http://localhost:8080")
	log.Fatal(http.ListenAndServe(":8080", handler))
}

/*
Message Handler
*/
func handleMessage(store *ParkingStore, msg mqtt.Message) {
	topic := msg.Topic()
	payload := string(msg.Payload())
	distance, ok := extractDistance(payload)

	status := "unknown"
	if ok {
		status = calculateStatus(distance)
	}

	var slotID int
	fmt.Sscanf(topic, "parking/slot/%d", &slotID)

	store.Update(slotID, status)
	log.Printf("Slot %d → %s (raw: %q)", slotID, status, payload)
}

func calculateStatus(distance float64) string {
	if distance < 2 || distance > 440 {
		return "unknown"
	} else if distance <= 20 {
		return "occupied"
	}
	return "available"
}

type payloadMsg struct {
	DistanceM float64 `json:"distance_m"`
}

func extractDistance(data string) (float64, bool) {
	// Find the JSON object within the log line
	// i := strings.Index(data, "{")
	// if i == -1 {
	// 	return 0, false
	// }

	// var msg payloadMsg
	// if err := json.Unmarshal([]byte(data[i:]), &msg); err != nil {
	// 	return 0, false
	// }

	// return msg.DistanceM, trued
	var distance float64 = 0
	distance, err := strconv.ParseFloat(data, 64)
	if err != nil {
		return 0, false
	} else {
		return distance, true
	}
}
