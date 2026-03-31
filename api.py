from fastapi import FastAPI, Depends
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates
from fastapi import Request
from pydantic import BaseModel
from sqlalchemy import create_engine, Column, Integer, String, Boolean
from sqlalchemy.orm import sessionmaker, declarative_base, Session
import os

# ---- DATABASE CONFIG ----

DATABASE_URL = os.getenv("DATABASE_URL", "sqlite:///./test.db")
# For Render PostgreSQL use:
# DATABASE_URL = os.getenv("DATABASE_URL")

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

# Dependency
def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()

# ---- POST: Add RFID ----

class RFIDRequest(BaseModel):
    rfid: str

@app.post("/scan")
def scan_card(data: RFIDRequest, db: Session = Depends(get_db)):
    existing = db.query(User).filter(User.rfid == data.rfid).first()
    if existing:
        return {"message": "RFID already exists"}

    new_user = User(rfid=data.rfid)
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