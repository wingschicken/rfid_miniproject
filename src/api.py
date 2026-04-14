from fastapi import FastAPI, Depends, HTTPException, Request
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
from pydantic import BaseModel
from sqlalchemy import create_engine, Column, Integer, String, Boolean
from sqlalchemy.orm import sessionmaker, declarative_base, Session
import os
import hmac
import hashlib
import time

# ---- DATABASE CONFIG ----

DATABASE_URL = os.getenv("DATABASE_URL", "sqlite:///./test.db")

apikey = os.getenv("ESP1_SECRET")

if apikey is None:
    raise Exception("ESP1_SECRET is not set")

apikey = apikey.encode()



engine = create_engine(
    DATABASE_URL,
    connect_args={"check_same_thread": False} if "sqlite" in DATABASE_URL else {},
    pool_pre_ping=True,
    pool_recycle=300,
    pool_size=5,
    max_overflow=10,
)
SessionLocal = sessionmaker(bind=engine)
Base = declarative_base()

# ---- MODEL ----

class User(Base):
    __tablename__ = "users"

    id = Column(Integer, primary_key=True, index=True)
    name = Column(String, nullable=True)
    rfid = Column(String, unique=True, index=True, nullable=False)
    isadmin = Column(Boolean, default=False)

Base.metadata.create_all(bind=engine)

# ---- FASTAPI ----

app = FastAPI()
templates = Jinja2Templates(directory="templates")

# ---- SECURITY CONFIG ----

# 🔐 Match Arduino
DEVICE_KEYS = {
    "esp_1": apikey
}

MAX_TIME_DIFF = 30  # seconds


def verify_signature(device_id, uid, timestamp, signature):
    if device_id not in DEVICE_KEYS:
        return False

    secret = DEVICE_KEYS[device_id]

    # MUST MATCH Arduino EXACTLY
    payload = f"{device_id}|{uid}|{timestamp}".encode()

    expected = hmac.new(
        secret,
        payload,
        hashlib.sha256
    ).hexdigest()

    return hmac.compare_digest(expected, signature)


# ---- DEPENDENCY ----

def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


# ---- REQUEST MODEL (NEW) ----

class ScanRequest(BaseModel):
    device_id: str
    uid: str
    timestamp: int
    signature: str


# ---- POST: Add RFID (SECURE) ----

@app.post("/scan")
def scan_card(data: ScanRequest, db: Session = Depends(get_db)):
    # ⏱️ Anti-replay
    now = int(time.time())
    if abs(now - data.timestamp) > MAX_TIME_DIFF:
        raise HTTPException(status_code=401, detail="Request expired")

    # 🔐 Verify HMAC
    if not verify_signature(data.device_id, data.uid, data.timestamp, data.signature):
        raise HTTPException(status_code=401, detail="Invalid signature")

    # ✅ Check if exists
    existing = db.query(User).filter(User.rfid == data.uid).first()
    if existing:
        return {"message": "RFID already exists"}

    # ✅ Save new RFID
    new_user = User(rfid=data.uid)
    db.add(new_user)
    db.commit()
    db.refresh(new_user)

    return {"message": "RFID added", "rfid": new_user.rfid}


# ---- GET: Check RFID ----

@app.get("/check/{rfid}")
def check_rfid(rfid: str, db: Session = Depends(get_db)):
    user = db.query(User).filter(User.rfid == rfid).first()
    if user:
        return {"status": "accept"}
    return {"status": "denied"}


# ---- GET: All Users ----

@app.get("/users")
def get_all_users(db: Session = Depends(get_db)):
    users = db.query(User).all()
    return users


# ---- WEBPAGE ----

@app.get("/", response_class=HTMLResponse)
def homepage(request: Request, db: Session = Depends(get_db)):
    users = db.query(User).order_by(User.id.desc()).all()
    return templates.TemplateResponse("index.html", {
        "request": request,
        "users": users
    })