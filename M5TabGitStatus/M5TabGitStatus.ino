#include <M5GFX.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "WIFISSID";
const char* password = "WIFIPASSWORD";

// GitHub Status API URL
const char* host = "www.githubstatus.com";
const char* apiPath = "/api/v2/components.json";

WiFiClientSecure client;

// Display settings
const int HEADER_HEIGHT = 60;
const int ROW_HEIGHT = 55;  // Increased for larger text
const int MARGIN = 15;
const int CIRCLE_RADIUS = 18;  // Status circle radius
const int CIRCLE_X_OFFSET = 40;  // Position from right edge

// Colors
const uint16_t COLOR_BG = TFT_BLACK;
const uint16_t COLOR_HEADER = TFT_NAVY;
const uint16_t COLOR_TEXT = TFT_WHITE;
const uint16_t COLOR_OPERATIONAL = TFT_GREEN;
const uint16_t COLOR_DEGRADED = TFT_YELLOW;
const uint16_t COLOR_PARTIAL = TFT_ORANGE;
const uint16_t COLOR_MAJOR = TFT_RED;
const uint16_t COLOR_UNKNOWN = TFT_DARKGREY;

struct Component {
  String name;
  String status;
};

Component components[30];
int componentCount = 0;
unsigned long lastUpdate = 0;
bool touchHandled = false;
int currentPage = 0;
int itemsPerPage = 0;

// Function declarations
void connectToWiFi();
void showMessage(const char* title, const char* message);
void fetchAndDisplayStatus();
void drawHeader(const char* statusText);
void drawComponents();
uint16_t getStatusColor(String status);

void setup() {
  Serial.begin(115200);
  
  // Initialize M5Tab
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1); // Landscape mode
  M5.Display.fillScreen(COLOR_BG);
  
  // Calculate items per page based on screen size
  itemsPerPage = (M5.Display.height() - HEADER_HEIGHT - MARGIN * 2) / ROW_HEIGHT;
  
  // Show startup screen
  showMessage("GitHub Status Monitor", "Initializing...");
  
  // Connect to WiFi
  connectToWiFi();
  
  // Initial fetch
  fetchAndDisplayStatus();
}

void connectToWiFi() {
  showMessage("Connecting to WiFi", ssid);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
    
    // Show progress dots
    M5.Display.fillRect(0, M5.Display.height() - 50, M5.Display.width(), 50, COLOR_BG);
    M5.Display.setCursor(MARGIN, M5.Display.height() - 35);
    M5.Display.setTextColor(COLOR_TEXT);
    M5.Display.setTextSize(3);
    String dots = "";
    for (int i = 0; i < (attempts % 4); i++) {
      dots += ".";
    }
    M5.Display.print(dots);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    showMessage("WiFi Connected", WiFi.localIP().toString().c_str());
    delay(1500);
  } else {
    showMessage("WiFi Failed", "Check credentials");
    delay(3000);
  }
}

void showMessage(const char* title, const char* message) {
  M5.Display.fillScreen(COLOR_BG);
  
  // Draw title
  M5.Display.setTextColor(COLOR_TEXT);
  M5.Display.setTextSize(4);
  int titleWidth = M5.Display.textWidth(title);
  M5.Display.setCursor((M5.Display.width() - titleWidth) / 2, M5.Display.height() / 2 - 50);
  M5.Display.print(title);
  
  // Draw message
  M5.Display.setTextSize(3);
  int msgWidth = M5.Display.textWidth(message);
  M5.Display.setCursor((M5.Display.width() - msgWidth) / 2, M5.Display.height() / 2 + 10);
  M5.Display.print(message);
}

void fetchAndDisplayStatus() {
  Serial.println("Fetching GitHub status...");
  
  // Show loading message in header
  drawHeader("Updating...");
  
  client.setInsecure();
  
  if (!client.connect(host, 443)) {
    showMessage("Connection Failed", "Cannot reach API");
    delay(3000);
    drawComponents(); // Show last known status
    return;
  }
  
  // Send HTTP request
  client.print("GET ");
  client.print(apiPath);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(host);
  client.println("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:147.0) Gecko/20100101 Firefox/147.0");
  client.println("Accept: application/json");
  client.println("Connection: close");
  client.println();
  
  // Wait for response
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      client.stop();
      showMessage("Request Timeout", "API not responding");
      delay(3000);
      drawComponents();
      return;
    }
  }
  
  // Skip headers
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) break;
  }
  
  // Read JSON
  String jsonResponse = "";
  while (client.available()) {
    jsonResponse += client.readString();
  }
  client.stop();
  
  // Parse JSON
  DynamicJsonDocument doc(6133);
  DeserializationError error = deserializeJson(doc, jsonResponse);
  
  if (error) {
    showMessage("Parse Error", error.c_str());
    delay(3000);
    drawComponents();
    return;
  }
  
  // Clear and update components
  componentCount = 0;
  JsonArray jsonComponents = doc["components"];
  
  for (JsonObject component : jsonComponents) {
    const char* name = component["name"];
    const char* status = component["status"];
    
    if (name && status) {
      String nameStr = String(name);
      nameStr.toLowerCase();
      
      // Skip if contains "for more information"
      if (nameStr.indexOf("for more information") != -1) {
        continue;
      }
      
      if (componentCount < 30) {
        components[componentCount].name = String(name);
        components[componentCount].status = String(status);
        componentCount++;
      }
    }
  }
  
  lastUpdate = millis();
  currentPage = 0; // Reset to first page after update
  drawComponents();
}

void drawHeader(const char* statusText = nullptr) {
  // Draw header background
  M5.Display.fillRect(0, 0, M5.Display.width(), HEADER_HEIGHT, COLOR_HEADER);
  
  // Draw title
  M5.Display.setTextColor(COLOR_TEXT);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(MARGIN, MARGIN);
  M5.Display.print("GitHub Status");
  
  // Draw update time or status
  M5.Display.setTextSize(2);
  if (statusText) {
    int textWidth = M5.Display.textWidth(statusText);
    M5.Display.setCursor(M5.Display.width() - textWidth - MARGIN, MARGIN);
    M5.Display.print(statusText);
  } else if (lastUpdate > 0) {
    unsigned long elapsed = (millis() - lastUpdate) / 1000;
    char timeStr[20];
    if (elapsed < 60) {
      sprintf(timeStr, "%lus ago", elapsed);
    } else {
      sprintf(timeStr, "%lum ago", elapsed / 60);
    }
    int textWidth = M5.Display.textWidth(timeStr);
    M5.Display.setCursor(M5.Display.width() - textWidth - MARGIN, MARGIN);
    M5.Display.print(timeStr);
  }
  
  // Draw refresh hint
  M5.Display.setTextSize(2);
  M5.Display.setCursor(M5.Display.width() - 200, MARGIN + 25);
  M5.Display.print("Touch to refresh");
}

void drawComponents() {
  M5.Display.fillScreen(COLOR_BG);
  drawHeader();
  
  if (componentCount == 0) {
    showMessage("No Components", "Touch to refresh");
    return;
  }
  
  // Calculate pagination
  int totalPages = (componentCount + itemsPerPage - 1) / itemsPerPage;
  int startIndex = currentPage * itemsPerPage;
  int endIndex = min(startIndex + itemsPerPage, componentCount);
  
  // Draw components for current page
  int yPos = HEADER_HEIGHT + MARGIN;
  
  for (int i = startIndex; i < endIndex; i++) {
    // Calculate positions
    int centerY = yPos + ROW_HEIGHT / 2 - 5;  // Center vertically in row
    int circleX = M5.Display.width() - CIRCLE_X_OFFSET;  // Circle X position
    
    // Draw component name - larger text
    M5.Display.setTextSize(3);  // Increased from 2 to 3
    M5.Display.setTextColor(COLOR_TEXT);
    M5.Display.setCursor(MARGIN, yPos + 5);  // Adjusted Y position for larger text
    
    // Truncate name if too long
    String displayName = components[i].name;
    int maxNameWidth = M5.Display.width() - (CIRCLE_X_OFFSET + CIRCLE_RADIUS + MARGIN * 2);
    
    while (M5.Display.textWidth(displayName) > maxNameWidth && displayName.length() > 0) {
      displayName = displayName.substring(0, displayName.length() - 1);
    }
    if (displayName != components[i].name) {
      displayName += "...";
    }
    
    M5.Display.print(displayName);
    
    // Draw status circle with appropriate color
    uint16_t statusColor = getStatusColor(components[i].status);
    
    // Draw filled circle for status
    M5.Display.fillCircle(circleX, centerY, CIRCLE_RADIUS, statusColor);
    
    // Add a border to make the circle more visible
    M5.Display.drawCircle(circleX, centerY, CIRCLE_RADIUS, COLOR_TEXT);
    M5.Display.drawCircle(circleX, centerY, CIRCLE_RADIUS - 1, COLOR_TEXT);
    
    // Draw separator line
    M5.Display.drawLine(MARGIN, yPos + ROW_HEIGHT - 5, M5.Display.width() - MARGIN, yPos + ROW_HEIGHT - 5, TFT_DARKGREY);
    
    yPos += ROW_HEIGHT;
  }
  
  // Draw footer with component count and page info
  int footerY = M5.Display.height() - 30;
  M5.Display.fillRect(0, footerY - 5, M5.Display.width(), 35, COLOR_BG);
  
  M5.Display.setTextColor(COLOR_TEXT);
  M5.Display.setTextSize(2);
  
  // Component count on the left
  char countStr[50];
  sprintf(countStr, "%d components", componentCount);
  M5.Display.setCursor(MARGIN, footerY);
  M5.Display.print(countStr);
  
  // Page info on the right if multiple pages
  if (totalPages > 1) {
    char pageStr[30];
    sprintf(pageStr, "Page %d/%d", currentPage + 1, totalPages);
    int pageWidth = M5.Display.textWidth(pageStr);
    M5.Display.setCursor(M5.Display.width() - pageWidth - MARGIN, footerY);
    M5.Display.print(pageStr);
    
    // Draw navigation hint in center
    M5.Display.setTextSize(1);
    String navHint = "Swipe for pages";
    int hintWidth = M5.Display.textWidth(navHint);
    M5.Display.setCursor((M5.Display.width() - hintWidth) / 2, footerY + 2);
    M5.Display.print(navHint);
  }
  
  // Draw battery voltage and percentage in bottom right corner
  float voltage = M5.Power.getBatteryVoltage();
  // For 2S LiPo: 6.4V (0%) to 8.4V (100%)
  int batteryPercent = (int)(((voltage - 6.4) / (8.4 - 6.4)) * 100.0);
  if (batteryPercent > 100) batteryPercent = 100;
  if (batteryPercent < 0) batteryPercent = 0;
  char batteryStr[40];
  sprintf(batteryStr, "Battery: %.2fV / %d%%", voltage, batteryPercent);
  int batteryWidth = M5.Display.textWidth(batteryStr);
  M5.Display.setTextColor(COLOR_TEXT);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(M5.Display.width() - batteryWidth - MARGIN, footerY);
  M5.Display.print(batteryStr);
}

uint16_t getStatusColor(String status) {
  status.toLowerCase();
  
  if (status.indexOf("operational") != -1) {
    return COLOR_OPERATIONAL;
  } else if (status.indexOf("degraded") != -1) {
    return COLOR_DEGRADED;
  } else if (status.indexOf("partial") != -1) {
    return COLOR_PARTIAL;
  } else if (status.indexOf("major") != -1) {
    return COLOR_MAJOR;
  }
  
  return COLOR_UNKNOWN;
}

void loop() {
  M5.update();
  
  // Handle touch input
  auto touchCount = M5.Touch.getCount();
  if (touchCount > 0) {
    if (!touchHandled) {
      auto touch = M5.Touch.getDetail(0);
      static int touchStartX = touch.x;
      static unsigned long touchStartTime = millis();
      if (touch.state == m5::touch_state_t::touch_begin) {
        touchHandled = true;
        
        // Check if swipe or tap
        if (touch.state == m5::touch_state_t::touch_begin) {
          touchStartX = touch.x;
          touchStartTime = millis();
        }
      } else if (touch.state == m5::touch_state_t::touch_end) {
        int swipeDistance = touch.x - touchStartX;
        unsigned long touchDuration = millis() - touchStartTime;
        
        if (abs(swipeDistance) > 50 && touchDuration < 500) {
          // Swipe detected - change page
          int totalPages = (componentCount + itemsPerPage - 1) / itemsPerPage;
          if (swipeDistance > 0 && currentPage > 0) {
            currentPage--;
            drawComponents();
          } else if (swipeDistance < 0 && currentPage < totalPages - 1) {
            currentPage++;
            drawComponents();
          }
        } else {
          // Tap detected - refresh
          fetchAndDisplayStatus();
        }
      }
    }
  } else {
    touchHandled = false;
  }
  
  // Auto-refresh every 5 minutes
  if (millis() - lastUpdate > 300000 && lastUpdate > 0) {
    fetchAndDisplayStatus();
  }
  
  // Update elapsed time every 30 seconds
  static unsigned long lastTimeUpdate = 0;
  if (millis() - lastTimeUpdate > 30000) {
    lastTimeUpdate = millis();
    if (componentCount > 0) {
      drawHeader();
    }
  }
  
  delay(50);
}