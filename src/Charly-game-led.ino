#include <WiFi.h>
#include <WebServer.h>
#include "FS.h"
#include "LittleFS.h"

#define DS1 5    // Data pour 74HC595 #1
#define SHCP1 7  // Shift Clock pour 74HC595 #1
#define STCP1 9  // Latch Clock pour 74HC595 #1

#define DS2 18    // Data pour 74HC595 #2
#define SHCP2 33  // Shift Clock pour 74HC595 #2
#define STCP2 35  // Latch Clock pour 74HC595 #2

#define BUTTON 12  // GPIO du bouton-poussoir

#define MAX_SCORES 5  // Nombre maximum de meilleurs scores enregistrés

volatile bool buttonPressed = false;          // Variable pour détecter l'appui bouton en IRQ
volatile unsigned long lastDebounceTime = 0;  // Pour l'anti-rebond
const unsigned long debounceDelay = 300;      // Temps minimum entre 2 appuis (50ms)

const char *ssid = "Charly-Game";   // Nom du réseau WiFi
const char *password = "12345678";  // Mot de passe (minimum 8 caractères)

int mode = 0;  // Mode d'affichage des LEDs
uint8_t randLedToMatch = 0;
uint8_t findLed = 0;
int defilement = 1000;
int i_defil = 0;
int score = 0;  // 🏆 Variable pour stocker le score

WebServer server(80);  // Serveur web sur le port 80

// Fonction ISR appelée en interruption avec anti-rebond
void IRAM_ATTR handleButtonPress() {
  unsigned long currentTime = millis();
  if (currentTime - lastDebounceTime > debounceDelay) {  // Vérification du rebond
    buttonPressed = true;
    lastDebounceTime = currentTime;
  }
}

void setup() {
  Serial.begin(115200);       // Initialisation du moniteur série
  randomSeed(analogRead(0));  // Initialisation du générateur de nombres aléatoires

  pinMode(DS1, OUTPUT);
  pinMode(SHCP1, OUTPUT);
  pinMode(STCP1, OUTPUT);

  pinMode(DS2, OUTPUT);
  pinMode(SHCP2, OUTPUT);
  pinMode(STCP2, OUTPUT);

  pinMode(BUTTON, INPUT);                              // Activation du pull-down interne
  attachInterrupt(BUTTON, handleButtonPress, RISING);  // Déclenchement sur un appui (front montant)

  // Mode Access Point (AP)
  WiFi.softAP(ssid, password);

  // Routes web
  server.on("/", handleRoot);
  server.on("/score", handleScore);  // Route pour récupérer le score en AJAX
  server.on("/scores", handleScores);  // Route pour récupérer la table de meilleurs scores en AJAX
  server.on("/resetScores", handleResetScores);  // Ajoute la route pour réinitialiser les scores

  server.begin();
  Serial.println("✅ Serveur Web démarré !");

  delay(1000);

  // 📁 Démarrer LittleFS AVEC FORMATAGE AUTOMATIQUE en cas d’échec
  if (!LittleFS.begin(true)) {
    Serial.println("❌ Erreur : LittleFS n'a pas pu être initialisé !");
  } else {
    Serial.println("✅ LittleFS est prêt !");
  }

  init_game();
}

// Fonction pour envoyer des données à un 74HC595 spécifique
void shiftOutData(int dsPin, int shcpPin, int stcpPin, uint8_t data) {
  digitalWrite(stcpPin, LOW);  // Désactiver le latch

  for (int i = 7; i >= 0; i--) {
    digitalWrite(shcpPin, LOW);
    digitalWrite(dsPin, (data >> i) & 1);
    digitalWrite(shcpPin, HIGH);
  }

  digitalWrite(stcpPin, HIGH);  // Activer le latch
}

// Fonction qui initialise le jeu
// c'est à dire :
// - allume aleatoirement une led sur le shift register 2
// - initialise le shift register 1 en partant de la premiere led
void init_game() {
  uint8_t previousRand = randLedToMatch;

  // 🔄 Générer une nouvelle LED cible différente de la précédente
  do {
    randLedToMatch = 1 << random(0, 8);
  } while (randLedToMatch == previousRand);  // Assure que la nouvelle valeur est différente

  shiftOutData(DS2, SHCP2, STCP2, randLedToMatch);
  findLed = 0;  // Réinitialisation correcte
}

int adjustToMultiple(int num) {
  int result = num / 2;
  int remainder5 = result % 5;
  int remainder10 = result % 10;

  if (remainder10 == 0) return result;
  if (remainder5 == 0) return result;

  int down = result - remainder5;
  int up = down + 5;

  return (result - down < up - result) ? down : up;
}

// Effet spécial en cas d'échec
void lose_animation() {
  // Clignotement rapide de la LED cible sur le registre #2
  for (uint8_t i = 0; i < 3; i++) {
    shiftOutData(DS2, SHCP2, STCP2, 0xFF);  // Toutes les LEDs s'allument (rouge)
    delay(100);
    shiftOutData(DS2, SHCP2, STCP2, 0x00);  // Toutes les LEDs s'éteignent
    delay(100);
  }

  // Effet "chute" des LEDs sur le registre #1 (de droite à gauche)
  for (int i = 7; i >= 0; i--) {
    shiftOutData(DS1, SHCP1, STCP1, 1 << i);
    delay(100);
  }

  // Pause dramatique avant redémarrage du jeu
  delay(500);
}

// Effet spécial en cas de victoire
void win_animation() {
  // 🟢 1ère étape : Allumer du centre vers les bords
  uint8_t ledPattern = 0b00000000;  // Commencer avec toutes les LEDs éteintes
  for (int i = 0; i < 4; i++) {
    ledPattern |= (1 << (3 - i)) | (1 << (4 + i));  // Allumer une paire de LEDs symétriquement
    shiftOutData(DS1, SHCP1, STCP1, ledPattern);
    shiftOutData(DS2, SHCP2, STCP2, ledPattern);
    delay(100);  // Vitesse de l'effet
  }

  // 🔴 2ème étape : Clignotement final de toutes les LEDs
  for (int i = 0; i < 3; i++) {
    shiftOutData(DS1, SHCP1, STCP1, 0xFF);  // Allumer toutes les LEDs
    shiftOutData(DS2, SHCP2, STCP2, 0xFF);
    delay(150);
    shiftOutData(DS1, SHCP1, STCP1, 0x00);  // Éteindre toutes les LEDs
    shiftOutData(DS2, SHCP2, STCP2, 0x00);
    delay(150);
  }
  // Pause avant redémarrage du jeu
  delay(200);
}

int adjustProgressively(int num) {
  if (num <= 50) return 50;  // Ne descend pas sous 50 ms (évite que ça devienne injouable)

  float factor = 0.7;         // Facteur de réduction (ajuster entre 0.7 et 0.9 pour tester)
  int result = num * factor;  // Réduction progressive

  return result - (result % 5);  // Arrondi au multiple de 5
}

// fonction de fin d'animation
void end_animation(bool victory) {
  if (victory) {
    win_animation();
    // ajuste le défilement en cas de victoire
    defilement = adjustProgressively(defilement);
    score += 10;
    //onWin();  // ✅ Gère l'incrémentation et la sauvegarde
  } else {
    lose_animation();
    // Ré-initialise le défilement en cas d'echec
    defilement = 1000;
    saveNewScore(score);  // ✅ Sauvegarde avant de remettre à zéro
    score = 0;
  }
}

// Fonction d'affichage de la page web
void handleRoot() {
  server.send(200, "text/html", pageHTML());
}

// 📡 Gestion du score en temps réel (AJAX)
void handleScore() {
  server.send(200, "text/plain", String(score));
}

// 📡 Gestion du score en temps réel (AJAX)
void handleScores() {
  String scoreData = getScores();
  server.send(200, "text/plain", scoreData);
}

// 📡 Gestion de la réinitialisation des scores via un bouton
void handleResetScores() {
  Serial.println("🔄 Réinitialisation des meilleurs scores...");

  File file = LittleFS.open("/scores.txt", "w");  // Écraser le fichier
  if (!file) {
    server.send(500, "text/plain", "❌ Erreur : Impossible de réinitialiser les scores.");
    return;
  }
  file.println("0");  // Ajouter un score par défaut pour éviter un fichier vide
  file.close();

  Serial.println("✅ Les meilleurs scores ont été réinitialisés !");
  server.send(200, "text/plain", "✅ Meilleurs scores réinitialisés !");
}


// 📜 Fonction pour récupérer les meilleurs scores depuis le fichier
String getScores() {
  // 🔍 Vérifier si le fichier existe
  if (!LittleFS.exists("/scores.txt")) {
    //Serial.println("⚠ Fichier scores.txt introuvable, création en cours...");
    File newFile = LittleFS.open("/scores.txt", "w");
    if (!newFile) {
      //Serial.println("❌ Impossible de créer le fichier des scores.");
      return "❌ Impossible de créer le fichier.";
    }
    newFile.println("0");  // Ajoute un score par défaut
    newFile.close();
    return "0";  // Retourne le score par défaut
  }

  // 📖 Ouvrir le fichier en lecture
  File file = LittleFS.open("/scores.txt", "r");
  if (!file) {
    //Serial.println("❌ Erreur de lecture du fichier.");
    return "❌ Erreur de lecture du fichier.";
  }

  // 🔍 Vérifier si le fichier est vide
  if (file.size() == 0) {
    Serial.println("⚠ Fichier vide, ajout d'un score par défaut...");
    file.close();
    File newFile = LittleFS.open("/scores.txt", "w");
    if (newFile) {
      newFile.println("0");
      newFile.close();
    }
    return "0";
  }

  String scores = "";
  int lineCount = 0;  // Nombre de scores lus

  while (file.available()) {
    String score = file.readStringUntil('\n');  // Lire chaque ligne
    score.trim();  // 🔄 Supprimer les espaces ou lignes vides
    if (score.length() > 0) {  // Vérifier si la ligne est valide
      scores += String(lineCount + 1) + ".\t" + score + "\n";  // Ajouter la position (classement)
      lineCount++;
    }
  }

  file.close();

  if (lineCount == 0) {  // Vérifier si on a bien lu des scores
    //Serial.println("⚠ Aucun score valide trouvé.");
    return "Aucun score enregistré.";
  }

  //Serial.println("📜 Scores récupérés :");
  //Serial.println(scores);
  
  return scores;
}

// ✅ Ajoute le score actuel à la liste si c'est un des 5 meilleurs
void saveNewScore(int newScore) {
  int scores[MAX_SCORES] = { 0 };
  int count = 0;

  // 📖 Lire les scores existants et les stocker dans le tableau
  File file = LittleFS.open("/scores.txt", "r");
  if (file) {
    while (file.available() && count < MAX_SCORES) {
      scores[count] = file.readStringUntil('\n').toInt();
      count++;
    }
    file.close();
  }

  // 🔄 Vérifier si le nouveau score doit être ajouté
  bool inserted = false;
  for (int i = 0; i < count; i++) {
    if (newScore > scores[i]) {  
      // 🔄 Décaler les scores pour insérer le nouveau score
      for (int j = MAX_SCORES - 1; j > i; j--) {
        scores[j] = scores[j - 1];
      }
      scores[i] = newScore;
      inserted = true;
      break;
    }
  }

  // 📁 ÉCRASER l'ancien fichier avec les 5 meilleurs scores
  file = LittleFS.open("/scores.txt", "w");
  for (int i = 0; i < min(count + (inserted ? 1 : 0), MAX_SCORES); i++) {
    file.println(scores[i]);
  }
  file.close();
}

// ✅ Lorsqu'une victoire est obtenue, le score actuel augmente et est comparé aux meilleurs scores
void onWin() {
  score += 10;  // 🔄 Incrémente le score actuel
  saveNewScore(score);  // ✅ Enregistre dans la liste triée
  Serial.print("🎉 Nouveau score enregistré : ");
  Serial.println(score);
}

// 🌐 Nouvelle interface web simplifiée avec correction AJAX
//String pageHTML() {
//  return String("<!DOCTYPE html><html lang='fr'>") +
//         "<head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>" +
//         "<title>Score du Jeu</title>" +
//         "<style>" +
//         "body { font-family: Arial, sans-serif; text-align: center; background-color: #121212; color: white; margin: 0; padding: 0; }" +
//         "h1 { padding: 15px; font-size: 28px; }" +
//         ".container { max-width: 400px; margin: auto; padding: 20px; }" +
//         ".score { font-size: 60px; font-weight: bold; color: #ff9800; }" +
//         ".score-list { font-size: 20px; font-weight: bold; color: white; margin-top: 20px; }" +
//         ".score-title { font-size: 24px; color: #4caf50; margin-top: 40px; }" +
//         "</style>" +
//         "<script>" +
//         "function updateScore() {" +
//         "fetch('/score')" +  // 🔄 Récupérer le SCORE ACTUEL
//         ".then(response => response.text())" +
//         ".then(data => document.getElementById('score').innerText = data);" +
//         "}" +
//         "function updateScores() {" +
//         "fetch('/scores')" +  // 🔄 Récupérer les MEILLEURS SCORES
//         ".then(response => response.text())" +
//         ".then(data => document.getElementById('scoreList').innerText = data);" +
//         "}" +
//         "setInterval(updateScore, 1000);" +  // Mise à jour du score actuel toutes les 1s
//         "setInterval(updateScores, 2000);" + // Mise à jour des meilleurs scores toutes les 2s
//         "</script>" +
//         "</head>" +
//         "<body>" +
//         "<h1>🎮 Score du Jeu 🎮</h1>" +
//         "<div class='container'>" +
//         "<p>Score actuel :</p>" +
//         "<span id='score' class='score'>0</span>" +  // 🏆 Score mis à jour en temps réel
//         "<h2 class='score-title'>🏆 Meilleurs Scores</h2>" +
//         "<pre id='scoreList' class='score-list'>Chargement...</pre>" +  // 🔄 Meilleurs scores
//         "</div></body></html>";
//}

// 🌐 Nouvelle interface web améliorée avec un joli bouton de réinitialisation
String pageHTML() {
  return String("<!DOCTYPE html><html lang='fr'>") +
         "<head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>" +
         "<title>Score du Jeu</title>" +
         "<style>" +
         "body { font-family: Arial, sans-serif; text-align: center; background-color: #121212; color: white; margin: 0; padding: 0; }" +
         "h1 { padding: 15px; font-size: 28px; }" +
         ".container { max-width: 400px; margin: auto; padding: 20px; }" +
         ".score { font-size: 60px; font-weight: bold; color: #ff9800; }" +
         ".score-list { font-size: 20px; font-weight: bold; color: white; margin-top: 20px; }" +
         ".score-title { font-size: 24px; color: #4caf50; margin-top: 40px; }" +
         
         /* 🎨 Bouton stylisé */
         ".reset-btn { background: linear-gradient(45deg, #ff4444, #cc0000); color: white; " +
         "padding: 12px 25px; font-size: 18px; border: none; border-radius: 8px; " +
         "cursor: pointer; margin-top: 20px; transition: 0.3s ease-in-out; box-shadow: 0 4px 8px rgba(255, 0, 0, 0.3); }" +
         ".reset-btn:hover { background: linear-gradient(45deg, #cc0000, #990000); " +
         "transform: scale(1.1); box-shadow: 0 6px 12px rgba(255, 0, 0, 0.5); }" +

         "</style>" +
         "<script>" +
         "function updateScore() {" +
         "fetch('/score')" +  
         ".then(response => response.text())" +
         ".then(data => document.getElementById('score').innerText = data);" +
         "}" +
         "function updateScores() {" +
         "fetch('/scores')" +  
         ".then(response => response.text())" +
         ".then(data => document.getElementById('scoreList').innerText = data);" +
         "}" +
         "function resetScores() {" +
         "if (confirm('⚠ Voulez-vous vraiment réinitialiser les meilleurs scores ?')) {" +  // 🔥 Ajout d'une confirmation
         "fetch('/resetScores')" +  
         ".then(response => response.text())" +
         ".then(data => { alert(data); updateScores(); });" +
         "}" +
         "}" +
         "setInterval(updateScore, 1000);" +  
         "setInterval(updateScores, 2000);" +  
         "</script>" +
         "</head>" +
         "<body>" +
         "<h1>🎮 Score du Jeu 🎮</h1>" +
         "<div class='container'>" +
         "<p>Score actuel :</p>" +
         "<span id='score' class='score'>0</span>" +  
         "<h2 class='score-title'>🏆 Meilleurs Scores</h2>" +
         "<pre id='scoreList' class='score-list'>Chargement...</pre>" +  
         "<button class='reset-btn' onclick='resetScores()'>🔄 Réinitialiser les scores</button>" +  
         "</div></body></html>";
}

void loop() {
  server.handleClient();  // Gérer les requêtes web
                          // 🟢 Gestion du chenillard
  if (i_defil >= defilement) {
    shiftOutData(DS1, SHCP1, STCP1, (1 << findLed));
  }

  // 🔵 Vérification du bouton (Victoire ou Défaite)
  if (buttonPressed) {
    buttonPressed = false;  // Reset du flag

    // 🌟 Ajustement de la synchronisation avec `findLed - 1`
    uint8_t shiftedValue = (findLed > 0) ? (1 << (findLed - 1)) : 128;

    // 🌟 SYNCHRONISER AVEC RANDLED 🌟
    if (randLedToMatch == shiftedValue) {
      Serial.println("✔ Victoire !");
      end_animation(true);
    } else {
      Serial.println("❌ Défaite...");
      end_animation(false);
    }
    init_game();  // 🔄 Réinitialiser après la victoire ou défaite
    return;       // Évite d'exécuter la suite inutilement
  }

  // 🟢 Gestion du chenillard
  if (i_defil >= defilement) {
    findLed = (findLed + 1) % 8;  // Boucle circulaire 0 → 7
    i_defil = 0;
  } else {
    i_defil += 5;
  }

  delay(5);  // Petit délai pour éviter une boucle trop rapide
}
