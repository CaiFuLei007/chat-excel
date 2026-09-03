/**
 * auth.js — 登录注册页逻辑
 * - 登录/注册同页 Tab 切换，记住上次登录方式
 * - 密码登录 / 邮箱验证码登录 / 注册（含唯一性校验、验证码 60s 倒计时）
 * 依赖：config.js / utils.js / api.js
 */

/* ============================ 文案 ============================ */
I18N.register(
  {
    'auth.welcome': '欢迎回来',
    'auth.welcomeSub': '登录后开始用自然语言分析你的数据',
    'auth.login': '登录',
    'auth.register': '注册',
    'auth.passwdLogin': '账号密码登录',
    'auth.vcodeLogin': '邮箱验证码登录',
    'auth.account': '账号',
    'auth.accountPh': '昵称或邮箱',
    'auth.password': '密码',
    'auth.passwordPh': '请输入密码',
    'auth.regPwdPh': '至少 6 位',
    'auth.password2': '确认密码',
    'auth.password2Ph': '再次输入密码',
    'auth.email': '邮箱',
    'auth.emailPh': 'name@example.com',
    'auth.vcode': '邮箱验证码',
    'auth.vcodePh': '6 位数字',
    'auth.sendCode': '获取验证码',
    'auth.resendIn': '{s}s 后重发',
    'auth.loginBtn': '登录',
    'auth.registerBtn': '注册',
    'auth.nickname': '用户名',
    'auth.nicknamePh': '设置你的昵称',
    'auth.secDivider': '设置密码',
    'auth.backPrefix': '想先了解一下产品？',
    'auth.backHome': '返回产品展示页',
    'auth.switchToReg': '还没有账号？',
    'auth.switchToRegBtn': '立即注册',
    'auth.switchToLogin': '已有账号？',
    'auth.switchToLoginBtn': '直接登录',
    'auth.errAccount': '请输入账号',
    'auth.errPassword': '请输入密码',
    'auth.errPwdMin': '密码至少6位以上',
    'auth.errPwdMismatch': '两次输入的密码不一致',
    'auth.errEmail': '请输入正确的邮箱格式',
    'auth.errVcode': '验证码为6位数字',
    'auth.errNickname': '请输入用户名',
    'auth.errNicknameTaken': '用户名已存在',
    'auth.errEmailTaken': '邮箱已存在',
    'auth.codeSent': '验证码已发送至邮箱，1分钟内有效',
    'auth.registerOk': '注册成功，请登录',
    'auth.loginOk': '登录成功',
    'auth.needCode': '请先获取验证码'
  },
  {
    'auth.welcome': 'Welcome back',
    'auth.welcomeSub': 'Sign in to analyze your data with natural language',
    'auth.login': 'Sign In',
    'auth.register': 'Sign Up',
    'auth.passwdLogin': 'Password',
    'auth.vcodeLogin': 'Email Code',
    'auth.account': 'Account',
    'auth.accountPh': 'Nickname or email',
    'auth.password': 'Password',
    'auth.passwordPh': 'Enter your password',
    'auth.regPwdPh': 'At least 6 characters',
    'auth.password2': 'Confirm password',
    'auth.password2Ph': 'Enter password again',
    'auth.email': 'Email',
    'auth.emailPh': 'name@example.com',
    'auth.vcode': 'Email code',
    'auth.vcodePh': '6-digit code',
    'auth.sendCode': 'Get Code',
    'auth.resendIn': 'Resend in {s}s',
    'auth.loginBtn': 'Sign In',
    'auth.registerBtn': 'Sign Up',
    'auth.nickname': 'Username',
    'auth.nicknamePh': 'Choose a nickname',
    'auth.secDivider': 'Set password',
    'auth.backPrefix': 'Want to learn about the product first?',
    'auth.backHome': 'Back to home page',
    'auth.switchToReg': "Don't have an account?",
    'auth.switchToRegBtn': 'Sign up',
    'auth.switchToLogin': 'Already have an account?',
    'auth.switchToLoginBtn': 'Sign in',
    'auth.errAccount': 'Please enter your account',
    'auth.errPassword': 'Please enter your password',
    'auth.errPwdMin': 'Password must be at least 6 characters',
    'auth.errPwdMismatch': 'Passwords do not match',
    'auth.errEmail': 'Please enter a valid email address',
    'auth.errVcode': 'Verification code must be 6 digits',
    'auth.errNickname': 'Please enter a username',
    'auth.errNicknameTaken': 'Username already exists',
    'auth.errEmailTaken': 'Email already exists',
    'auth.codeSent': 'Code sent to your email, valid for 1 minute',
    'auth.registerOk': 'Registered successfully, please sign in',
    'auth.loginOk': 'Signed in successfully',
    'auth.needCode': 'Please get the verification code first'
  }
);

/* ============================ 状态 ============================ */
const AuthState = {
  vcodeLoginCodeId: '', // 验证码登录的 codeId
  regCodeId: '', // 注册的 codeId
  timers: {}
};

const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
const VCODE_RE = /^\d{6}$/;

/* ============================ 表单辅助 ============================ */

function fieldGroup(form, name) {
  return form.querySelector(`.form-group[data-field="${name}"]`);
}

function setFieldError(form, name, msg) {
  const g = fieldGroup(form, name);
  if (!g) return;
  const input = g.querySelector('.input');
  const err = g.querySelector('.error-msg');
  if (msg) {
    g.classList.add('error');
    input && input.classList.add('has-error');
    if (err) err.textContent = msg;
  } else {
    g.classList.remove('error');
    input && input.classList.remove('has-error');
    if (err) err.textContent = '';
  }
}

function focusFirstError(form) {
  const g = form.querySelector('.form-group.error .input');
  g && g.focus();
}

/** 验证码按钮 60s 倒计时 */
function startCountdown(btn, key) {
  clearInterval(AuthState.timers[key]);
  let remain = CONFIG.CODE_COUNTDOWN;
  btn.disabled = true;
  const render = () => {
    btn.textContent = I18N.t('auth.resendIn').replace('{s}', remain);
  };
  render();
  AuthState.timers[key] = setInterval(() => {
    remain -= 1;
    if (remain <= 0) {
      clearInterval(AuthState.timers[key]);
      btn.disabled = false;
      btn.textContent = I18N.t('auth.sendCode');
    } else {
      render();
    }
  }, 1000);
}

/** 发送验证码（登录 / 注册共用） */
async function sendVerifyCode(emailInput, btn, timerKey, onOk) {
  const email = emailInput.value.trim();
  const form = emailInput.closest('form');
  if (!EMAIL_RE.test(email)) {
    setFieldError(form, emailInput.closest('.form-group').dataset.field, I18N.t('auth.errEmail'));
    emailInput.focus();
    return;
  }
  btn.disabled = true;
  try {
    const result = await API.sendCode(email);
    onOk(result.codeId);
    toast(I18N.t('auth.codeSent'), 'success');
    startCountdown(btn, timerKey);
  } catch (e) {
    btn.disabled = false;
  }
}

/* ============================ Tab 切换 ============================ */

function switchAuthTab(tab) {
  document.querySelectorAll('.auth-tab').forEach((b) => {
    b.classList.toggle('active', b.dataset.authTab === tab);
  });
  document.getElementById('panel-login').style.display = tab === 'login' ? '' : 'none';
  document.getElementById('panel-register').style.display = tab === 'register' ? '' : 'none';

  // 切换时清空所有表单数据和错误提示
  document.querySelectorAll('.auth-panel form').forEach((f) => {
    f.reset();
    f.querySelectorAll('.form-group').forEach((g) => {
      g.classList.remove('error');
      const input = g.querySelector('.input');
      input && input.classList.remove('has-error');
      const err = g.querySelector('.error-msg');
      if (err) err.textContent = '';
    });
  });
  document.getElementById('reg-code-tip').classList.remove('show');
  AuthState.vcodeLoginCodeId = '';
  AuthState.regCodeId = '';

  renderSwitchNote(tab);
}

function renderSwitchNote(tab) {
  const note = document.getElementById('auth-switch-note');
  if (tab === 'login') {
    note.innerHTML = `${escapeHtml(I18N.t('auth.switchToReg'))} <button type="button" data-goto="register">${escapeHtml(I18N.t('auth.switchToRegBtn'))}</button>`;
  } else {
    note.innerHTML = `${escapeHtml(I18N.t('auth.switchToLogin'))} <button type="button" data-goto="login">${escapeHtml(I18N.t('auth.switchToLoginBtn'))}</button>`;
  }
}

function switchLoginMethod(method) {
  document.querySelectorAll('.login-method-tab').forEach((b) => {
    b.classList.toggle('active', b.dataset.method === method);
  });
  document.getElementById('form-passwd-login').style.display = method === 'passwd' ? '' : 'none';
  document.getElementById('form-vcode-login').style.display = method === 'vcode' ? '' : 'none';
  localStorage.setItem(CONFIG.KEYS.LOGIN_TAB, method);
}

/* ============================ 登录提交 ============================ */

async function submitPasswdLogin(form) {
  const username = form.username.value.trim();
  const password = form.password.value;
  let ok = true;
  if (!username) { setFieldError(form, 'username', I18N.t('auth.errAccount')); ok = false; }
  if (!password) { setFieldError(form, 'password', I18N.t('auth.errPassword')); ok = false; }
  if (!ok) { focusFirstError(form); return; }

  const btn = form.querySelector('.auth-submit');
  btn.disabled = true;
  try {
    const result = await API.passwdLogin(username, password);
    setSessionId(result.sessionId);
    toast(I18N.t('auth.loginOk'), 'success');
    setTimeout(() => { window.location.href = 'console.html'; }, 400);
  } catch (e) {
    btn.disabled = false;
  }
}

async function submitVcodeLogin(form) {
  const email = form.email.value.trim();
  const code = form.verifyCode.value.trim();
  let ok = true;
  if (!EMAIL_RE.test(email)) { setFieldError(form, 'email', I18N.t('auth.errEmail')); ok = false; }
  if (!VCODE_RE.test(code)) { setFieldError(form, 'verifyCode', I18N.t('auth.errVcode')); ok = false; }
  if (!ok) { focusFirstError(form); return; }
  if (!AuthState.vcodeLoginCodeId) { toast(I18N.t('auth.needCode'), 'error'); return; }

  const btn = form.querySelector('.auth-submit');
  btn.disabled = true;
  try {
    const result = await API.vcodeLogin(email, code, AuthState.vcodeLoginCodeId);
    setSessionId(result.sessionId);
    toast(I18N.t('auth.loginOk'), 'success');
    setTimeout(() => { window.location.href = 'console.html'; }, 400);
  } catch (e) {
    btn.disabled = false;
  }
}

/* ============================ 注册 ============================ */

async function validateNickname(input) {
  const form = input.closest('form');
  const v = input.value.trim();
  if (!v) { setFieldError(form, 'nickname', I18N.t('auth.errNickname')); return false; }
  try {
    await API.validNickname(v);
    setFieldError(form, 'nickname', '');
    return true;
  } catch (e) {
    setFieldError(form, 'nickname', e.errorMsg || I18N.t('auth.errNicknameTaken'));
    return false;
  }
}

async function validateRegEmail(input) {
  const form = input.closest('form');
  const v = input.value.trim();
  if (!EMAIL_RE.test(v)) { setFieldError(form, 'email', I18N.t('auth.errEmail')); return false; }
  try {
    await API.validEmail(v);
    setFieldError(form, 'email', '');
    return true;
  } catch (e) {
    setFieldError(form, 'email', e.errorMsg || I18N.t('auth.errEmailTaken'));
    return false;
  }
}

async function submitRegister(form) {
  const nickname = form.nickname.value.trim();
  const email = form.email.value.trim();
  const code = form.verifyCode.value.trim();
  const pwd = form.password.value;
  const pwd2 = form.password2.value;

  let ok = true;
  if (!nickname) { setFieldError(form, 'nickname', I18N.t('auth.errNickname')); ok = false; }
  if (!EMAIL_RE.test(email)) { setFieldError(form, 'email', I18N.t('auth.errEmail')); ok = false; }
  if (!VCODE_RE.test(code)) { setFieldError(form, 'regCode', I18N.t('auth.errVcode')); ok = false; }
  if (pwd.length < 6) { setFieldError(form, 'password', I18N.t('auth.errPwdMin')); ok = false; }
  if (pwd2 !== pwd) { setFieldError(form, 'password2', I18N.t('auth.errPwdMismatch')); ok = false; }
  if (!ok) { focusFirstError(form); return; }
  if (!AuthState.regCodeId) { toast(I18N.t('auth.needCode'), 'error'); return; }

  const btn = form.querySelector('.auth-submit');
  btn.disabled = true;
  try {
    await API.register({ nickname, password: pwd, email, verifyCode: code, codeId: AuthState.regCodeId });
    toast(I18N.t('auth.registerOk'), 'success');
    // 注册成功 → 自动切换到登录 Tab（默认账号密码）
    switchAuthTab('login');
    switchLoginMethod('passwd');
  } catch (e) {
    btn.disabled = false;
  }
}

/* ============================ 初始化 ============================ */

document.addEventListener('DOMContentLoaded', () => {
  // Logo 图标
  document.getElementById('auth-nav-logo').innerHTML = icon('logo');
  document.getElementById('auth-logo').innerHTML = icon('logo');

  // 密码显示/隐藏按钮图标
  document.querySelectorAll('[data-toggle-pwd]').forEach((btn) => {
    btn.innerHTML = icon('eye');
  });

  I18N.apply();
  I18N.bindSwitch(() => {
    renderSwitchNote(document.querySelector('.auth-tab.active').dataset.authTab);
  });

  // 已登录则直接进控制台
  if (getSessionId()) {
    window.location.href = 'console.html';
    return;
  }

  // 主 Tab
  document.querySelectorAll('.auth-tab').forEach((btn) => {
    btn.addEventListener('click', () => switchAuthTab(btn.dataset.authTab));
  });

  // 底部切换入口
  document.getElementById('auth-switch-note').addEventListener('click', (e) => {
    const goto = e.target && e.target.dataset ? e.target.dataset.goto : null;
    if (goto) switchAuthTab(goto);
  });

  // 登录方式子 Tab
  document.querySelectorAll('.login-method-tab').forEach((btn) => {
    btn.addEventListener('click', () => switchLoginMethod(btn.dataset.method));
  });
  // 记住上次登录方式
  const lastMethod = localStorage.getItem(CONFIG.KEYS.LOGIN_TAB);
  if (lastMethod === 'vcode') switchLoginMethod('vcode');

  /* ---- 密码显示/隐藏 ---- */
  document.querySelectorAll('[data-toggle-pwd]').forEach((btn) => {
    btn.addEventListener('click', () => {
      const input = btn.closest('.input-wrap').querySelector('.input');
      const show = input.type === 'password';
      input.type = show ? 'text' : 'password';
      btn.innerHTML = icon(show ? 'eyeOff' : 'eye');
    });
  });

  /* ---- 密码登录 ---- */
  const passwdForm = document.getElementById('form-passwd-login');
  passwdForm.addEventListener('submit', (e) => {
    e.preventDefault();
    submitPasswdLogin(passwdForm);
  });
  passwdForm.username.addEventListener('blur', () => {
    setFieldError(passwdForm, 'username', passwdForm.username.value.trim() ? '' : I18N.t('auth.errAccount'));
  });
  passwdForm.password.addEventListener('blur', () => {
    setFieldError(passwdForm, 'password', passwdForm.password.value ? '' : I18N.t('auth.errPassword'));
  });

  /* ---- 验证码登录 ---- */
  const vcodeForm = document.getElementById('form-vcode-login');
  vcodeForm.addEventListener('submit', (e) => {
    e.preventDefault();
    submitVcodeLogin(vcodeForm);
  });
  vcodeForm.email.addEventListener('blur', () => {
    const v = vcodeForm.email.value.trim();
    if (v && !EMAIL_RE.test(v)) setFieldError(vcodeForm, 'email', I18N.t('auth.errEmail'));
    else setFieldError(vcodeForm, 'email', '');
  });
  vcodeForm.verifyCode.addEventListener('blur', () => {
    const v = vcodeForm.verifyCode.value.trim();
    if (v && !VCODE_RE.test(v)) setFieldError(vcodeForm, 'verifyCode', I18N.t('auth.errVcode'));
    else setFieldError(vcodeForm, 'verifyCode', '');
  });
  document.getElementById('vcode-login-send').addEventListener('click', (e) => {
    sendVerifyCode(vcodeForm.email, e.currentTarget, 'vcodeLogin', (codeId) => {
      AuthState.vcodeLoginCodeId = codeId;
    });
  });

  /* ---- 注册 ---- */
  const regForm = document.getElementById('form-register');
  regForm.addEventListener('submit', (e) => {
    e.preventDefault();
    submitRegister(regForm);
  });
  regForm.nickname.addEventListener('blur', () => {
    if (regForm.nickname.value.trim()) validateNickname(regForm.nickname);
    else setFieldError(regForm, 'nickname', I18N.t('auth.errNickname'));
  });
  regForm.email.addEventListener('blur', () => {
    if (regForm.email.value.trim()) validateRegEmail(regForm.email);
    else setFieldError(regForm, 'email', I18N.t('auth.errEmail'));
  });
  regForm.password.addEventListener('blur', () => {
    const v = regForm.password.value;
    if (v && v.length < 6) setFieldError(regForm, 'password', I18N.t('auth.errPwdMin'));
    else setFieldError(regForm, 'password', '');
  });
  regForm.password2.addEventListener('blur', () => {
    const v = regForm.password2.value;
    if (v && v !== regForm.password.value) setFieldError(regForm, 'password2', I18N.t('auth.errPwdMismatch'));
    else setFieldError(regForm, 'password2', '');
  });
  regForm.verifyCode.addEventListener('blur', () => {
    const v = regForm.verifyCode.value.trim();
    if (v && !VCODE_RE.test(v)) setFieldError(regForm, 'regCode', I18N.t('auth.errVcode'));
    else setFieldError(regForm, 'regCode', '');
  });
  document.getElementById('reg-send').addEventListener('click', (e) => {
    sendVerifyCode(regForm.email, e.currentTarget, 'register', (codeId) => {
      AuthState.regCodeId = codeId;
      const tip = document.getElementById('reg-code-tip');
      tip.innerHTML = `${icon('check')}<span>${escapeHtml(I18N.t('auth.codeSent'))}</span>`;
      tip.classList.add('show');
    });
  });

  renderSwitchNote('login');
});
