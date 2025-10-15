
from fastapi import FastAPI
from pydantic import BaseModel

class User(BaseModel):
    name: str
    email: str

app = FastAPI()

@app.post("/users/")
async def create_user(user: User):
    return {"message": f"User {user.name} with email {user.email} created successfully"}
