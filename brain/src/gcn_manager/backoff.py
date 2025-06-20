import random


class BackOff:
    def next(self) -> float:
        raise NotImplementedError

    def reset(self) -> None:
        raise NotImplementedError


class ExponentialFullRandomBackOff(BackOff):
    def __init__(self, base: int, cap: int) -> None:
        self.attempts = 0
        self.base = base
        self.cap = cap

    def next(self) -> float:
        # Interesting : https://aws.amazon.com/fr/blogs/architecture/exponential-backoff-and-jitter/
        self.attempts += 1
        if self.attempts >= self.cap:
            self.attempts = self.cap
        value = min(self.cap, self.base * 2 ** self.attempts)
        value = random.randint(self.base, value) + random.random()
        return value

    def reset(self) -> None:
        self.attempts = 0
