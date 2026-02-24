import { set, update } from "https://www.gstatic.com/firebasejs/10.8.0/firebase-database.js";
import { getDatabase, ref, get, query, orderByChild, equalTo } from "https://www.gstatic.com/firebasejs/10.8.0/firebase-database.js";
import { initializeApp } from "https://www.gstatic.com/firebasejs/10.8.0/firebase-app.js";
import {
    getAuth,
    createUserWithEmailAndPassword,
    signInWithEmailAndPassword,
    sendPasswordResetEmail,
    fetchSignInMethodsForEmail,
    updateProfile
} from "https://www.gstatic.com/firebasejs/10.8.0/firebase-auth.js";

const firebaseConfig = {
    apiKey: "AIzaSyCzWlIyMppyUBIeJVbDbWfu-ztohrxaTEo",
    authDomain: "esp32-babaald.firebaseapp.com",
    databaseURL: "https://esp32-babaald-default-rtdb.firebaseio.com",
    projectId: "esp32-babaald",
    storageBucket: "esp32-babaald.firebasestorage.app",
    messagingSenderId: "911018871293",
    appId: "1:911018871293:web:5e137fc1ca18b4c6a4b194"
};

const app  = initializeApp(firebaseConfig);
const auth = getAuth(app);
const db   = getDatabase(app);

const loader = document.getElementById('loading-overlay');
function showLoader() { loader.style.display = 'flex'; }
function hideLoader() { loader.style.display = 'none'; }

// Hide loader on page ready
window.addEventListener('load', function() {
    setTimeout(hideLoader, 800);
});

// ── UI Navigation ─────────────────────────────────────────────
window.toggleView = function(viewId) {
    document.querySelectorAll('.form-container').forEach(function(f){
        f.classList.add('hidden'); f.classList.remove('active');
    });
    var t = document.getElementById(viewId);
    if (t) { t.classList.remove('hidden'); t.classList.add('active'); }
    // Clear conflict box on view change
    var cb = document.getElementById('conflict-box');
    if (cb) cb.classList.remove('show');
};

window.hideConflict = function() {
    var cb = document.getElementById('conflict-box');
    if (cb) cb.classList.remove('show');
};

// ── Password Visibility (defined in HTML inline, but also here as backup) ──
window.togglePw = function(inputId, btnId) {
    var inp = document.getElementById(inputId);
    var btn = document.getElementById(btnId);
    if (!inp) return;
    if (inp.type === 'password') { inp.type = 'text'; if (btn) btn.style.color = '#e8341a'; }
    else                         { inp.type = 'password'; if (btn) btn.style.color = '#999'; }
};

// Show eye icon when password field is focused
['login-password','signup-password','signup-confirm'].forEach(function(id) {
    var inp = document.getElementById(id);
    var eyeId = id === 'login-password' ? 'login-eye' : id === 'signup-password' ? 'signup-eye' : 'confirm-eye';
    if (!inp) return;
    inp.addEventListener('focus', function() {
        var e = document.getElementById(eyeId);
        if (e) { e.style.opacity = '1'; e.style.pointerEvents = 'all'; }
    });
    inp.addEventListener('blur', function() {
        setTimeout(function() {
            var e = document.getElementById(eyeId);
            if (e) { e.style.opacity = '0'; e.style.pointerEvents = 'none'; }
        }, 200);
    });
});

// ── Password Match Check ──────────────────────────────────────
window.checkPwMatch = function() {
    var p1  = document.getElementById('signup-password').value;
    var p2  = document.getElementById('signup-confirm').value;
    var msg = document.getElementById('pw-match-msg');
    if (!p2) { msg.className = 'pw-match-msg'; return; }
    if (p1 === p2) { msg.innerHTML = '&#10003; Passwords match'; msg.className = 'pw-match-msg ok'; }
    else           { msg.innerHTML = '&#10007; Passwords do not match'; msg.className = 'pw-match-msg err'; }
};

// ── Signup ────────────────────────────────────────────────────
document.getElementById('signup-form').addEventListener('submit', async function(e) {
    e.preventDefault();

    var name     = document.getElementById('signup-name').value.trim();
    var email    = document.getElementById('signup-email').value.trim();
    var phone    = document.getElementById('signup-phone').value.trim();
    var password = document.getElementById('signup-password').value;
    var confirm  = document.getElementById('signup-confirm').value;

    // Password match check
    if (password !== confirm) {
        alert('Passwords do not match. Please re-enter.');
        return;
    }
    if (password.length < 6) {
        alert('Password must be at least 6 characters.');
        return;
    }

    showLoader();

    // Check if email already registered
    try {
        var methods = await fetchSignInMethodsForEmail(auth, email);
        if (methods && methods.length > 0) {
            hideLoader();
            var cb  = document.getElementById('conflict-box');
            var msg = document.getElementById('conflict-msg');
            msg.textContent = 'An account with the email "' + email + '" already exists. Would you like to log in, or reset your password if you have forgotten it?';
            cb.classList.add('show');
            return;
        }
    } catch(err) {
        // If fetchSignInMethods fails (e.g. network), proceed cautiously
        hideLoader();
        alert('Could not verify email: ' + err.message);
        return;
    }

    try {
        var cred = await createUserWithEmailAndPassword(auth, email, password);
        var user = cred.user;
        await updateProfile(user, { displayName: name });

        var userData = { fullName: name, email: email };
        if (phone) userData.phone = phone;
        await set(ref(db, 'users/' + user.uid), userData);

        hideLoader();
        alert('Account created for ' + name + '! Welcome to BabaALD\'s World.');
        this.reset();
        document.getElementById('pw-match-msg').className = 'pw-match-msg';
        window.location.href = 'SmartDeviceControl.html';
    } catch(err) {
        hideLoader();
        alert('Signup Error: ' + err.message);
    }
});

// ── Login ─────────────────────────────────────────────────────
document.getElementById('login-form').addEventListener('submit', async function(e) {
    e.preventDefault();
    showLoader();

    var email    = document.getElementById('login-username').value.trim();
    var password = document.getElementById('login-password').value;

    try {
        var cred = await signInWithEmailAndPassword(auth, email, password);
        var user = cred.user;

        await update(ref(db, 'users/' + user.uid), { LED1R:111, LED1G:108, LED1B:108 });

        this.reset();
        window.location.href = 'SmartDeviceControl.html';
    } catch(err) {
        hideLoader();
        alert('Login Error: Invalid email or password. Please try again.');
    }
});

// ── Forgot Password ───────────────────────────────────────────
document.getElementById('forgot-form').addEventListener('submit', async function(e) {
    e.preventDefault();
    var email = document.getElementById('forgot-email').value.trim();
    showLoader();

    try {
        var usersRef   = ref(db, 'users');
        var emailQuery = query(usersRef, orderByChild('email'), equalTo(email));
        var snapshot   = await get(emailQuery);

        if (snapshot.exists()) {
            await sendPasswordResetEmail(auth, email);
            hideLoader();
            alert('A password reset link has been sent to ' + email + '. Please check your inbox.');
            window.toggleView('login-section');
            this.reset();
        } else {
            hideLoader();
            alert('This email is not registered in BabaALD\'s World. Please enter a valid registered email.');
        }
    } catch(err) {
        hideLoader();
        alert('Reset Error: ' + err.message);
    }
});
