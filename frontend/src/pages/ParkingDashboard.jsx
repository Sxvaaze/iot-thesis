import React, { useState, useEffect } from "react";

const API_URL = "http://localhost:8080/api/parking";
const POLL_INTERVAL_MS = 2000; // Poll every 2 seconds

const ParkingDashboard = () => {
  const [slots, setSlots] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [lastUpdated, setLastUpdated] = useState(null);

  useEffect(() => {
    let isMounted = true;

    const fetchSlots = async () => {
      try {
        const response = await fetch(API_URL);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const data = await response.json();
        if (isMounted) {
          setSlots(data);
          setLastUpdated(new Date());
          setError(null);
        }
      } catch (err) {
        if (isMounted) setError(err.message);
      } finally {
        if (isMounted) setLoading(false);
      }
    };

    fetchSlots(); // Initial fetch
    const interval = setInterval(fetchSlots, POLL_INTERVAL_MS);

    return () => {
      isMounted = false;
      clearInterval(interval);
    };
  }, []);

  /* 
      Styles
  */
  const containerStyle = {
    maxWidth: "800px",
    margin: "40px auto",
    fontFamily: "'Segoe UI', Tahoma, Geneva, Verdana, sans-serif",
    padding: "0 20px",
  };

  const headerStyle = {
    textAlign: "center",
    color: "#1a1a2e",
    marginBottom: "8px",
    fontSize: "2rem",
  };

  const subtitleStyle = {
    textAlign: "center",
    color: "#666",
    marginBottom: "32px",
    fontSize: "0.9rem",
  };

  const gridStyle = {
    display: "grid",
    gridTemplateColumns: "repeat(auto-fit, minmax(160px, 1fr))",
    gap: "20px",
    marginBottom: "24px",
  };

  const getCardStyle = (status) => {
    const base = {
      borderRadius: "12px",
      padding: "24px 16px",
      textAlign: "center",
      boxShadow: "0 4px 12px rgba(0,0,0,0.08)",
      transition: "transform 0.2s, box-shadow 0.2s",
      cursor: "default",
      border: "2px solid transparent",
    };

    switch (status) {
      case "available":
        return {
          ...base,
          background: "linear-gradient(135deg, #d4edda, #c3e6cb)",
          borderColor: "#28a745",
        };
      case "occupied":
        return {
          ...base,
          background: "linear-gradient(135deg, #f8d7da, #f5c6cb)",
          borderColor: "#dc3545",
        };
      default:
        return {
          ...base,
          background: "linear-gradient(135deg, #e2e3e5, #d6d8db)",
          borderColor: "#6c757d",
        };
    }
  };

  const getStatusIcon = (status) => {
    switch (status) {
      case "available":
        return "🟢";
      case "occupied":
        return "🔴";
      default:
        return "⚪";
    }
  };

  const getStatusLabel = (status) => {
    switch (status) {
      case "available":
        return "Available";
      case "occupied":
        return "Occupied";
      default:
        return "Unknown";
    }
  };

  // Render
  if (loading) {
    return (
      <div style={containerStyle}>
        <h1 style={headerStyle}>Parking Status</h1>
        <p style={{ textAlign: "center", color: "#666" }}>Loading slot data...</p>
      </div>
    );
  }

  const availableCount = slots.filter((s) => s.status === "available").length;

  return (
    <div style={containerStyle}>
      <h1 style={headerStyle}>Parking Slot Status</h1>
      <p style={subtitleStyle}>
        Live data from MQTT Broker &middot;{" "}
        {availableCount} of {slots.length} slots available
      </p>

      {error && (
        <div
          style={{
            background: "#fff3cd",
            border: "1px solid #ffc107",
            borderRadius: "8px",
            padding: "12px",
            marginBottom: "20px",
            color: "#856404",
            textAlign: "center",
          }}
        >
          Connection error: {error}
        </div>
      )}

      <div style={gridStyle}>
        {slots.map((slot) => (
          <div key={slot.slot_id} style={getCardStyle(slot.status)}>
            <div style={{ fontSize: "2.5rem", marginBottom: "8px" }}>
              {getStatusIcon(slot.status)}
            </div>
            <h3 style={{ margin: "0 0 4px", color: "#333", fontSize: "1.1rem" }}>
              Slot {slot.slot_id}
            </h3>
            <p
              style={{
                margin: "0 0 8px",
                fontWeight: "bold",
                fontSize: "0.95rem",
                color:
                  slot.status === "available"
                    ? "#155724"
                    : slot.status === "occupied"
                    ? "#721c24"
                    : "#383d41",
              }}
            >
              {getStatusLabel(slot.status)}
            </p>
            <small style={{ color: "#666", fontSize: "0.75rem" }}>
              {new Date(slot.updated_at).toLocaleTimeString()}
            </small>
          </div>
        ))}
      </div>

      {lastUpdated && (
        <p style={{ textAlign: "center", color: "#999", fontSize: "0.8rem" }}>
          Last updated: {lastUpdated.toLocaleTimeString()} &middot; Refreshing
          every {POLL_INTERVAL_MS / 1000}s
        </p>
      )}
    </div>
  );
};

export default ParkingDashboard;
